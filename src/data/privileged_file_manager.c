/*
 * privileged_file_manager.c
 *
 * يستخدم `pkexec <self> --privileged <op> <args>` لتنفيذ العمليات المحمية.
 * المنطق الفعلي للعمليات موجود في helper/privileged_ops.c المدمج في التطبيق.
 */

#include "privileged_file_manager.h"
#include "../domain/file_entity.h"
#include <gio/gio.h>
#include <string.h>

#define PKEXEC_PATH "/usr/bin/pkexec"

/* ── مسار التطبيق الحالي (self) ─────────────────────────────────────── */

static const char *get_self_path(void) {
    static char cached[4096] = {0};
    if (cached[0]) return cached;

    /* قراءة مسار التطبيق من /proc/self/exe */
    ssize_t n = readlink("/proc/self/exe", cached, sizeof(cached) - 1);
    if (n > 0) {
        cached[n] = '\0';
        return cached;
    }
    /* fallback */
    g_strlcpy(cached, "/usr/bin/aetherfiles", sizeof(cached));
    return cached;
}

/* ── التحقق من التوفر ───────────────────────────────────────────────── */

gboolean aether_privileged_is_available(void) {
    return g_file_test(PKEXEC_PATH, G_FILE_TEST_IS_EXECUTABLE) &&
           g_file_test(get_self_path(), G_FILE_TEST_IS_EXECUTABLE);
}

/* ── بنيات البيانات الداخلية ────────────────────────────────────────── */

typedef enum { HELPER_MODE_LIST, HELPER_MODE_OP } HelperMode;

typedef struct {
    GTask       *task;
    GSubprocess *proc;
    HelperMode   mode;
    GByteArray  *buf;
    GInputStream *stream;
} ReadCtx;

/* ── تحليل سطر JSON من مخرجات list ──────────────────────────────────── */

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

static GListModel *build_model(const char *output) {
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

/* ── قراءة stdout بشكل غير متزامن ──────────────────────────────────── */

static void on_stdout_read(GObject *src, GAsyncResult *res, gpointer ud);

static void finish_read(ReadCtx *ctx, const char *output) {
    if (ctx->mode == HELPER_MODE_LIST) {
        GListModel *m = build_model(output);
        g_task_return_pointer(ctx->task, m, g_object_unref);
    } else {
        GError *err = NULL;
        if (check_op_output(output, &err))
            g_task_return_boolean(ctx->task, TRUE);
        else
            g_task_return_error(ctx->task, err);
    }
    g_object_unref(ctx->stream);
    g_object_unref(ctx->proc);
    g_object_unref(ctx->task);
    g_free(ctx);
}

static void on_stdout_read(GObject *src, GAsyncResult *res, gpointer ud) {
    ReadCtx *ctx = ud;
    GError  *err = NULL;
    GBytes  *bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(src), res, &err);

    if (err) {
        g_task_return_error(ctx->task, err);
        g_byte_array_free(ctx->buf, TRUE);
        g_object_unref(ctx->stream);
        g_object_unref(ctx->proc);
        g_object_unref(ctx->task);
        g_free(ctx);
        return;
    }

    gsize sz = 0;
    const guint8 *data = bytes ? g_bytes_get_data(bytes, &sz) : NULL;
    if (sz > 0) {
        g_byte_array_append(ctx->buf, data, sz);
        g_bytes_unref(bytes);
        g_input_stream_read_bytes_async(ctx->stream, 65536,
            G_PRIORITY_DEFAULT, g_task_get_cancellable(ctx->task),
            on_stdout_read, ctx);
        return;
    }
    if (bytes) g_bytes_unref(bytes);

    guint8 nul = 0;
    g_byte_array_append(ctx->buf, &nul, 1);
    char *output = (char *)g_byte_array_free(ctx->buf, FALSE);
    ctx->buf = NULL;

    finish_read(ctx, output);
    g_free(output);
}

/* ── تشغيل pkexec <self> --privileged <op> <args> ───────────────────── */

static void launch(GTask *task, HelperMode mode, const char **argv) {
    GError *err = NULL;
    GSubprocessLauncher *lnch = g_subprocess_launcher_new(
        G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_SILENCE);
    GSubprocess *proc = g_subprocess_launcher_spawnv(lnch, argv, &err);
    g_object_unref(lnch);

    if (!proc) {
        g_task_return_error(task, err);
        g_object_unref(task);
        return;
    }

    GInputStream *stream = g_subprocess_get_stdout_pipe(proc);
    ReadCtx *ctx = g_new0(ReadCtx, 1);
    ctx->task   = g_object_ref(task);
    ctx->proc   = proc;
    ctx->mode   = mode;
    ctx->buf    = g_byte_array_new();
    ctx->stream = g_object_ref(stream);

    g_input_stream_read_bytes_async(stream, 65536,
        G_PRIORITY_DEFAULT, g_task_get_cancellable(task),
        on_stdout_read, ctx);

    g_object_unref(task);
}

/* ── API العام ───────────────────────────────────────────────────────── */

void aether_privileged_list_async(const char *path, GCancellable *cancellable,
                                   GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, cancellable, cb, ud);
    const char *argv[] = { PKEXEC_PATH, get_self_path(), "--privileged",
                           "list", path, NULL };
    launch(task, HELPER_MODE_LIST, argv);
}

GListModel *aether_privileged_list_finish(GAsyncResult *res, GError **err) {
    return g_task_propagate_pointer(G_TASK(res), err);
}

void aether_privileged_copy_async(const char *src, const char *dst,
                                   GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    const char *argv[] = { PKEXEC_PATH, get_self_path(), "--privileged",
                           "copy", src, dst, NULL };
    launch(task, HELPER_MODE_OP, argv);
}

void aether_privileged_move_async(const char *src, const char *dst,
                                   GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    const char *argv[] = { PKEXEC_PATH, get_self_path(), "--privileged",
                           "move", src, dst, NULL };
    launch(task, HELPER_MODE_OP, argv);
}

void aether_privileged_delete_async(const char *path,
                                     GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    const char *argv[] = { PKEXEC_PATH, get_self_path(), "--privileged",
                           "delete", path, NULL };
    launch(task, HELPER_MODE_OP, argv);
}

void aether_privileged_mkdir_async(const char *path,
                                    GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    const char *argv[] = { PKEXEC_PATH, get_self_path(), "--privileged",
                           "mkdir", path, NULL };
    launch(task, HELPER_MODE_OP, argv);
}

void aether_privileged_touch_async(const char *path,
                                    GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    const char *argv[] = { PKEXEC_PATH, get_self_path(), "--privileged",
                           "touch", path, NULL };
    launch(task, HELPER_MODE_OP, argv);
}

void aether_privileged_compress_async(const char *format, const char *dest,
                                       GStrv sources, GAsyncReadyCallback cb,
                                       gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    guint n = sources ? g_strv_length(sources) : 0;
    /* pkexec self --privileged compress fmt dst src... NULL */
    const char **argv = g_new0(const char *, n + 7);
    int i = 0;
    argv[i++] = PKEXEC_PATH;
    argv[i++] = get_self_path();
    argv[i++] = "--privileged";
    argv[i++] = "compress";
    argv[i++] = format;
    argv[i++] = dest;
    for (guint j = 0; j < n; j++) argv[i++] = sources[j];
    argv[i] = NULL;
    launch(task, HELPER_MODE_OP, argv);
    g_free(argv);
}

void aether_privileged_extract_async(const char *archive, const char *dest,
                                      GAsyncReadyCallback cb, gpointer ud) {
    GTask *task = g_task_new(NULL, NULL, cb, ud);
    const char *argv[] = { PKEXEC_PATH, get_self_path(), "--privileged",
                           "extract", archive, dest, NULL };
    launch(task, HELPER_MODE_OP, argv);
}

gboolean aether_privileged_op_finish(GAsyncResult *res, GError **err) {
    return g_task_propagate_boolean(G_TASK(res), err);
}
