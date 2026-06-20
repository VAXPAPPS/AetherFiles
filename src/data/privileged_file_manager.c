/*
 * privileged_file_manager.c
 *
 * نموذج الـ daemon الدائم:
 *   - عند أول عملية محمية: يُطلق pkexec <self> --privileged-daemon
 *     (pkexec يطلب كلمة المرور مرة واحدة)
 *   - الـ daemon يبقى حياً ومتصلاً عبر pipe
 *   - كل العمليات اللاحقة ترسل أوامر إلى الـ daemon مباشرةً بدون pkexec
 *   - عند إغلاق التطبيق يُرسَل "quit" فيُنهي الـ daemon نفسه
 *
 * بروتوكول stdin → daemon:
 *   list\t<path>\n
 *   copy\t<src>\t<dst>\n
 *   move\t<src>\t<dst>\n
 *   delete\t<path>\n
 *   mkdir\t<path>\n
 *   touch\t<path>\n
 *   compress\t<fmt>\t<dst>\t<src1>\t<src2>...\n
 *   extract\t<archive>\t<dst>\n
 *   quit\n
 *
 * بروتوكول stdout ← daemon:
 *   READY\n                    (عند بدء التشغيل)
 *   {json...}\n               (لكل ملف في list)
 *   DONE\n                    (نهاية list)
 *   OK\n                      (نجاح عملية كتابية)
 *   ERR:<message>\n           (فشل)
 */

#define _GNU_SOURCE
#include "privileged_file_manager.h"
#include "../domain/file_entity.h"
#include <gio/gio.h>
#include <string.h>
#include <unistd.h>

#define PKEXEC_PATH "/usr/bin/pkexec"

/* ── مسار التطبيق الحالي ─────────────────────────────────────────────── */

static const char *get_self_path(void) {
    static char cached[4096] = {0};
    if (cached[0]) return cached;
    ssize_t n = readlink("/proc/self/exe", cached, sizeof(cached) - 1);
    if (n > 0) { cached[n] = '\0'; return cached; }
    g_strlcpy(cached, "/usr/bin/aetherfiles", sizeof(cached));
    return cached;
}

/* ── التحقق من التوفر ─────────────────────────────────────────────────── */

gboolean aether_privileged_is_available(void) {
    return g_file_test(PKEXEC_PATH, G_FILE_TEST_IS_EXECUTABLE) &&
           g_file_test(get_self_path(), G_FILE_TEST_IS_EXECUTABLE);
}

/* ═══════════════════════════════════════════════════════════════════════
 * إدارة الـ daemon الدائم
 * ═══════════════════════════════════════════════════════════════════════ */

typedef struct {
    GSubprocess  *proc;
    GOutputStream *stdin_stream;   /* نكتب الأوامر هنا */
    GInputStream  *stdout_stream;  /* نقرأ الردود منه  */
    gboolean       ready;          /* TRUE بعد استقبال READY */
} DaemonState;

static DaemonState s_daemon = {0};

typedef struct {
    char ch;
    gboolean done;
    gboolean error;
} ReadCharCtx;

static void on_read_char(GObject *src, GAsyncResult *res, gpointer ud) {
    ReadCharCtx *ctx = ud;
    gssize nr = g_input_stream_read_finish(G_INPUT_STREAM(src), res, NULL);
    ctx->done = TRUE;
    if (nr <= 0) ctx->error = TRUE;
}

/* قراءة سطر كامل بشكل متزامن من stream مع السماح للواجهة بالاستجابة */
static char *read_line_sync(GInputStream *stream) {
    GString *gs = g_string_new(NULL);
    ReadCharCtx ctx;
    while (TRUE) {
        ctx.done = FALSE;
        ctx.error = FALSE;
        g_input_stream_read_async(stream, &ctx.ch, 1, G_PRIORITY_DEFAULT, NULL, on_read_char, &ctx);
        while (!ctx.done) {
            g_main_context_iteration(NULL, TRUE);
        }
        if (ctx.error) break;
        if (ctx.ch == '\n') break;
        if (ctx.ch != '\r') g_string_append_c(gs, ctx.ch);
    }
    return g_string_free(gs, FALSE);
}

/* إطلاق الـ daemon عبر pkexec — يُستدعى مرة واحدة */
static gboolean daemon_start(void) {
    if (s_daemon.ready) return TRUE;

    const char *argv[] = {
        PKEXEC_PATH, get_self_path(), "--privileged-daemon", NULL
    };

    GError *err = NULL;
    GSubprocessLauncher *lnch = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    s_daemon.proc = g_subprocess_launcher_spawnv(lnch, argv, &err);
    g_object_unref(lnch);

    if (!s_daemon.proc) {
        g_printerr("daemon_start: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
        return FALSE;
    }

    s_daemon.stdin_stream  = g_subprocess_get_stdin_pipe(s_daemon.proc);
    s_daemon.stdout_stream = g_subprocess_get_stdout_pipe(s_daemon.proc);

    /* انتظر READY */
    char *line = read_line_sync(s_daemon.stdout_stream);
    gboolean ok = (g_strcmp0(line, "READY") == 0);
    g_free(line);

    if (!ok) {
        g_printerr("daemon_start: did not receive READY\n");
        g_object_unref(s_daemon.proc);
        s_daemon.proc = NULL;
        return FALSE;
    }

    s_daemon.ready = TRUE;

    /* إرسال "quit" عند انتهاء التطبيق لإغلاق الـ daemon بشكل نظيف */
    atexit(aether_privileged_session_end);

    return TRUE;
}

/* إرسال أمر نصي إلى الـ daemon */
static gboolean daemon_send(const char *cmd) {
    if (!s_daemon.ready) return FALSE;
    GError *err = NULL;
    gsize written;
    if (!g_output_stream_write_all(s_daemon.stdin_stream,
                                   cmd, strlen(cmd), &written, NULL, &err)) {
        g_printerr("daemon_send: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
        return FALSE;
    }
    g_output_stream_flush(s_daemon.stdin_stream, NULL, NULL);
    return TRUE;
}

/* إغلاق الـ daemon — يُستدعى تلقائياً عند atexit */
void aether_privileged_session_end(void) {
    if (!s_daemon.ready) return;
    daemon_send("quit\n");
    g_output_stream_close(s_daemon.stdin_stream, NULL, NULL);
    s_daemon.ready = FALSE;
    s_daemon.proc  = NULL;
}

/* ═══════════════════════════════════════════════════════════════════════
 * بنيات قراءة الردود (غير متزامن)
 * ═══════════════════════════════════════════════════════════════════════ */

typedef enum { HELPER_MODE_LIST, HELPER_MODE_OP } HelperMode;

typedef struct {
    GTask      *task;
    HelperMode  mode;
    GByteArray *buf;
} ReadCtx;

/* ── تحليل JSON ─────────────────────────────────────────────────────── */

static char *json_get(const char *json, const char *key, gboolean is_bool) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *p = strstr(json, search);
    if (!p) return NULL;
    p += strlen(search);
    while (*p == ' ') p++;
    if (is_bool) {
        if (!strncmp(p,"true",4))  return g_strdup("true");
        if (!strncmp(p,"false",5)) return g_strdup("false");
        return NULL;
    }
    if (*p == '"') {
        p++;
        GString *gs = g_string_new(NULL);
        while (*p && *p != '"') {
            if (*p == '\\' && *(p+1)) {
                p++;
                switch (*p) {
                case '"':  g_string_append_c(gs,'"');  break;
                case '\\': g_string_append_c(gs,'\\'); break;
                case 'n':  g_string_append_c(gs,'\n'); break;
                case 'r':  g_string_append_c(gs,'\r'); break;
                case 't':  g_string_append_c(gs,'\t'); break;
                default:   g_string_append_c(gs,*p);  break;
                }
            } else g_string_append_c(gs,*p);
            p++;
        }
        return g_string_free(gs, FALSE);
    }
    const char *end = p;
    while (*end && *end!=',' && *end!='}' && *end!='\n') end++;
    return g_strndup(p, end-p);
}

static AetherFileEntity *parse_line(const char *line) {
    if (!line || line[0] != '{') return NULL;
    char *name   = json_get(line,"name",  FALSE);
    char *path   = json_get(line,"path",  FALSE);
    char *is_dir = json_get(line,"is_dir",TRUE);
    char *size_s = json_get(line,"size",  FALSE);
    char *icon   = json_get(line,"icon",  FALSE);
    if (!name || !path) {
        g_free(name); g_free(path); g_free(is_dir); g_free(size_s); g_free(icon);
        return NULL;
    }
    gboolean dir = !g_strcmp0(is_dir,"true");
    goffset  sz  = size_s ? (goffset)g_ascii_strtoll(size_s,NULL,10) : 0;
    char *uri    = g_filename_to_uri(path, NULL, NULL);
    if (!uri) uri = g_strdup_printf("file://%s", path);
    AetherFileEntity *e = aether_file_entity_new(name, path, uri, sz, dir,
                                                  icon ? icon : "text-x-generic");
    g_free(name); g_free(path); g_free(is_dir); g_free(size_s); g_free(icon); g_free(uri);
    return e;
}

static GListModel *build_model_from_buf(const char *output) {
    GListStore *store = g_list_store_new(AETHER_TYPE_FILE_ENTITY);
    if (!output) return G_LIST_MODEL(store);
    char **lines = g_strsplit(output, "\n", -1);
    for (int i = 0; lines[i]; i++) {
        AetherFileEntity *e = parse_line(lines[i]);
        if (e) { g_list_store_append(store, e); g_object_unref(e); }
    }
    g_strfreev(lines);
    return G_LIST_MODEL(store);
}

static gboolean check_op_output(const char *out, GError **error) {
    if (!out || !out[0] || g_str_has_prefix(out,"OK")) return TRUE;
    if (g_str_has_prefix(out,"ERR:")) {
        g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", out + 4);
        return FALSE;
    }
    return TRUE;
}

/* ═══════════════════════════════════════════════════════════════════════
 * قراءة الردود بشكل غير متزامن
 * ═══════════════════════════════════════════════════════════════════════ */

static void on_daemon_read(GObject *src, GAsyncResult *res, gpointer ud);

static void finish_daemon_read(ReadCtx *ctx, const char *output) {
    if (ctx->mode == HELPER_MODE_LIST) {
        /* احذف سطر "DONE" من النهاية إذا وُجد */
        char *out = g_strdup(output ? output : "");
        /* إزالة DONE\n من النهاية */
        char *done_pos = g_strrstr(out, "\nDONE");
        if (!done_pos) done_pos = strstr(out, "DONE");
        if (done_pos) *done_pos = '\0';
        GListModel *m = build_model_from_buf(out);
        g_free(out);
        g_task_return_pointer(ctx->task, m, g_object_unref);
    } else {
        GError *err = NULL;
        if (check_op_output(output, &err))
            g_task_return_boolean(ctx->task, TRUE);
        else
            g_task_return_error(ctx->task, err);
    }
    g_object_unref(ctx->task);
    g_byte_array_free(ctx->buf, TRUE);
    g_free(ctx);
}

static void on_daemon_read(GObject *src, GAsyncResult *res, gpointer ud) {
    ReadCtx *ctx = ud;
    GError  *err = NULL;
    GBytes  *bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(src), res, &err);

    if (err) {
        g_task_return_error(ctx->task, err);
        g_byte_array_free(ctx->buf, TRUE);
        g_object_unref(ctx->task);
        g_free(ctx);
        return;
    }

    gsize sz = 0;
    const guint8 *data = bytes ? g_bytes_get_data(bytes, &sz) : NULL;

    if (sz > 0) {
        g_byte_array_append(ctx->buf, data, sz);
        g_bytes_unref(bytes);

        /* فحص: هل وصل المُنهي المناسب؟ */
        guint8 nul = 0;
        g_byte_array_append(ctx->buf, &nul, 1);
        const char *current = (const char *)ctx->buf->data;
        g_byte_array_remove_index(ctx->buf, ctx->buf->len - 1);

        gboolean done = FALSE;
        if (ctx->mode == HELPER_MODE_LIST) {
            done = (strstr(current, "\nDONE\n") != NULL ||
                    strstr(current, "DONE\n")   != NULL ||
                    g_str_has_suffix(current, "\nDONE") ||
                    g_strcmp0(current, "DONE")   == 0);
        } else {
            /* عملية كتابية: OK\n أو ERR:...\n */
            done = (strstr(current, "\nOK\n") != NULL ||
                    g_str_has_suffix(current, "\nOK") ||
                    g_str_has_prefix(current, "OK")   ||
                    strstr(current, "ERR:")     != NULL);
        }

        if (!done) {
            g_input_stream_read_bytes_async(s_daemon.stdout_stream, 65536,
                G_PRIORITY_DEFAULT, g_task_get_cancellable(ctx->task),
                on_daemon_read, ctx);
            return;
        }
    }
    if (bytes) g_bytes_unref(bytes);

    guint8 nul = 0;
    g_byte_array_append(ctx->buf, &nul, 1);
    char *output = (char *)g_byte_array_free(ctx->buf, FALSE);
    ctx->buf = NULL;
    /* إزالة \n الزائدة من النهاية */
    size_t olen = strlen(output);
    while (olen > 0 && (output[olen-1]=='\n'||output[olen-1]=='\r')) output[--olen]='\0';

    finish_daemon_read(ctx, output);
    g_free(output);
}

/* إرسال أمر وبدء قراءة الرد */
static void daemon_dispatch(GTask *task, HelperMode mode, const char *cmd) {
    /* إذا لم يكن الـ daemon يعمل، حاول تشغيله تلقائياً عبر pkexec */
    if (!s_daemon.ready) {
        if (!daemon_start()) {
            /* المستخدم رفض كلمة المرور أو فشل pkexec */
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED,
                                    "Authentication failed or was cancelled");
            g_object_unref(task);
            return;
        }
    }

    if (!daemon_send(cmd)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED,
                                "Failed to send command to daemon");
        g_object_unref(task);
        return;
    }

    ReadCtx *ctx = g_new0(ReadCtx, 1);
    ctx->task = g_object_ref(task);
    ctx->mode = mode;
    ctx->buf  = g_byte_array_new();

    g_input_stream_read_bytes_async(s_daemon.stdout_stream, 65536,
        G_PRIORITY_DEFAULT, g_task_get_cancellable(task),
        on_daemon_read, ctx);

    g_object_unref(task);
}

/* ═══════════════════════════════════════════════════════════════════════
 * تشغيل الـ daemon (يُستدعى من الطبقة العليا أول مرة)
 * ═══════════════════════════════════════════════════════════════════════ */

gboolean aether_privileged_daemon_start(void) {
    return daemon_start();
}

gboolean aether_privileged_daemon_is_running(void) {
    return s_daemon.ready;
}

/* ═══════════════════════════════════════════════════════════════════════
 * API العام
 * ═══════════════════════════════════════════════════════════════════════ */

void aether_privileged_list_async(const char *path, GCancellable *cancellable,
                                   GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, cancellable, cb, ud);
    char *cmd = g_strdup_printf("list\t%s\n", path);
    daemon_dispatch(task, HELPER_MODE_LIST, cmd);
    g_free(cmd);
}

GListModel *aether_privileged_list_finish(GAsyncResult *res, GError **err) {
    return g_task_propagate_pointer(G_TASK(res), err);
}

void aether_privileged_copy_async(const char *src, const char *dst,
                                   GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    char *cmd = g_strdup_printf("copy\t%s\t%s\n", src, dst);
    daemon_dispatch(task, HELPER_MODE_OP, cmd);
    g_free(cmd);
}

void aether_privileged_move_async(const char *src, const char *dst,
                                   GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    char *cmd = g_strdup_printf("move\t%s\t%s\n", src, dst);
    daemon_dispatch(task, HELPER_MODE_OP, cmd);
    g_free(cmd);
}

void aether_privileged_delete_async(const char *path,
                                     GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    char *cmd = g_strdup_printf("delete\t%s\n", path);
    daemon_dispatch(task, HELPER_MODE_OP, cmd);
    g_free(cmd);
}

void aether_privileged_mkdir_async(const char *path,
                                    GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    char *cmd = g_strdup_printf("mkdir\t%s\n", path);
    daemon_dispatch(task, HELPER_MODE_OP, cmd);
    g_free(cmd);
}

void aether_privileged_touch_async(const char *path,
                                    GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    char *cmd = g_strdup_printf("touch\t%s\n", path);
    daemon_dispatch(task, HELPER_MODE_OP, cmd);
    g_free(cmd);
}

void aether_privileged_compress_async(const char *format, const char *dest,
                                       GStrv sources, GAsyncReadyCallback cb,
                                       gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    GString *cmd = g_string_new("compress\t");
    g_string_append(cmd, format);
    g_string_append_c(cmd, '\t');
    g_string_append(cmd, dest);
    if (sources) {
        for (int i = 0; sources[i]; i++) {
            g_string_append_c(cmd, '\t');
            g_string_append(cmd, sources[i]);
        }
    }
    g_string_append_c(cmd, '\n');
    daemon_dispatch(task, HELPER_MODE_OP, cmd->str);
    g_string_free(cmd, TRUE);
}

void aether_privileged_extract_async(const char *archive, const char *dest,
                                      GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    char *cmd = g_strdup_printf("extract\t%s\t%s\n", archive, dest);
    daemon_dispatch(task, HELPER_MODE_OP, cmd);
    g_free(cmd);
}

gboolean aether_privileged_op_finish(GAsyncResult *res, GError **err) {
    return g_task_propagate_boolean(G_TASK(res), err);
}
