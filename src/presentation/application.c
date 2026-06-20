#include "application.h"
#include "views/window.h"
#include "views/bluetooth_share_dialog.h"
#include "controllers/clipboard_controller.h"
#include "../data/archive_manager.h"
#include "../data/privileged_file_manager.h"
#include "../theme_manager.h"
#include <gio/gio.h>
#include <string.h>
#include <unistd.h>

struct _AetherApplication {
    GtkApplication parent_instance;
    AetherClipboardController *clipboard;
};

G_DEFINE_TYPE(AetherApplication, aether_application, GTK_TYPE_APPLICATION)

static AetherWindow *get_active_win(GApplication *app) {
    GtkWindow *w = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (w && AETHER_IS_WINDOW(w)) return AETHER_WINDOW(w);
    return NULL;
}

static void aether_application_activate(GApplication *app) {
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (!window)
        window = aether_window_new(AETHER_APPLICATION(app));
    gtk_window_present(window);
}

static void aether_application_startup(GApplication *app) {
    G_APPLICATION_CLASS(aether_application_parent_class)->startup(app);
    g_object_set(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", TRUE, NULL);
    theme_manager_init();
}

/* ── open ── */
static void on_open_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    const char *path = g_variant_get_string(parameter, NULL);
    char *uri = g_filename_to_uri(path, NULL, NULL);
    if (uri) { g_app_info_launch_default_for_uri(uri, NULL, NULL); g_free(uri); }
}

/* ── cut / copy ── */
static void on_cut_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    GStrv paths = aether_window_get_selected_paths(win);
    if (paths) {
        aether_clipboard_set(app->clipboard, paths, AETHER_CLIPBOARD_CUT);
        g_strfreev(paths);
        /* إعادة تحميل الواجهة لتطبيق ستايل العناصر المقصوصة */
        aether_window_reload(win);
    }
}

static void on_copy_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    GStrv paths = aether_window_get_selected_paths(win);
    if (paths) {
        aether_clipboard_set(app->clipboard, paths, AETHER_CLIPBOARD_COPY);
        g_strfreev(paths);
    }
}

/* ── paste ── */
/* ── مساعد داخلي: إعادة تحميل بعد العملية المحمية ── */
static void on_privileged_op_done(GObject *src, GAsyncResult *res, gpointer ud) {
    (void)src;
    AetherWindow *win = AETHER_WINDOW(ud);
    aether_window_stop_progress(win);
    GError *err = NULL;
    if (!aether_privileged_op_finish(res, &err)) {
        g_printerr("Privileged operation failed: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
    }
    aether_window_reload(win);
}

/* ── بيانات لتمرير context كامل لعملية اللصق المحمية ── */
typedef struct {
    AetherApplication *app;
    char             **paths;   /* نسخة من paths الـ clipboard */
    char              *dest_dir;
    AetherClipboardOp  op;
    int                total;
    int                pending;
    gboolean           has_error;
} PrivPasteCtx;

static void on_priv_paste_file_done(GObject *src, GAsyncResult *res, gpointer ud) {
    (void)src;
    PrivPasteCtx *ctx = ud;
    GError *err = NULL;
    if (!aether_privileged_op_finish(res, &err)) {
        g_printerr("Privileged paste failed: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
        ctx->has_error = TRUE;
    }
    ctx->pending--;
    if (ctx->pending == 0) {
        AetherWindow *w = get_active_win(G_APPLICATION(ctx->app));
        if (w) {
            aether_window_stop_progress(w);
            aether_window_reload(w);
        }
        g_strfreev(ctx->paths);
        g_free(ctx->dest_dir);
        g_free(ctx);
    }
}

static void paste_privileged(AetherApplication *app,
                              GStrv              paths,
                              AetherClipboardOp  op,
                              const char        *dest_dir)
{
    int total = g_strv_length(paths);
    if (total == 0) return;

    PrivPasteCtx *ctx = g_new0(PrivPasteCtx, 1);
    ctx->app      = app;
    ctx->paths    = g_strdupv(paths);
    ctx->dest_dir = g_strdup(dest_dir);
    ctx->op       = op;
    ctx->total    = total;
    ctx->pending  = total;

    for (int i = 0; paths[i]; i++) {
        char *basename  = g_path_get_basename(paths[i]);
        char *dest_path = g_build_filename(dest_dir, basename, NULL);
        g_free(basename);

        if (op == AETHER_CLIPBOARD_COPY)
            aether_privileged_copy_async(paths[i], dest_path,
                on_priv_paste_file_done, ctx);
        else
            aether_privileged_move_async(paths[i], dest_path,
                on_priv_paste_file_done, ctx);

        g_free(dest_path);
    }
}

static void on_paste_done(GObject *src, GAsyncResult *res, gpointer ud) {
    (void)src;
    AetherApplication *app = AETHER_APPLICATION(ud);
    AetherWindow *w = get_active_win(G_APPLICATION(app));
    if (w) aether_window_stop_progress(w);

    GError *err = NULL;
    aether_clipboard_paste_finish(app->clipboard, res, &err);
    if (err) {
        /* عند PERMISSION_DENIED: أعد المحاولة عبر المساعد المحمي */
        if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
            aether_privileged_is_available() && w) {
            g_error_free(err);
            const char *dest = aether_window_get_current_path(w);
            GStrv paths      = aether_clipboard_get_paths(app->clipboard);
            AetherClipboardOp op = aether_clipboard_get_op(app->clipboard);
            if (dest && paths && paths[0]) {
                aether_window_start_progress(w);
                paste_privileged(app, paths, op, dest);
                return; /* إعادة التحميل ستحدث في on_priv_paste_file_done */
            }
        }
        g_printerr("Paste error: %s\n", err->message);
        g_error_free(err);
    }
    
    if (w) aether_window_reload(w);
}

/* ── بيانات حوار التعارض (من app.paste) ── */
typedef struct {
    AetherApplication *app;
    char              *dest_dir;
    GPtrArray         *conflict_names;
} AppConflictData;

static void app_conflict_data_free(AppConflictData *d) {
    g_free(d->dest_dir);
    g_ptr_array_free(d->conflict_names, TRUE);
    g_free(d);
}

static void on_app_conflict_response(GtkDialog *dlg, int response, gpointer user_data) {
    AppConflictData *d = user_data;
    AetherApplication *app = d->app;
    gtk_window_destroy(GTK_WINDOW(dlg));

    AetherWindow *win = get_active_win(G_APPLICATION(app));

    if (response == GTK_RESPONSE_ACCEPT) {
        /* Replace */
        if (win) aether_window_start_progress(win);
        aether_clipboard_paste_with_flags(app->clipboard, d->dest_dir,
                                          G_FILE_COPY_OVERWRITE,
                                          on_paste_done, app);
    } else if (response == GTK_RESPONSE_YES) {
        /* Keep Both */
        aether_clipboard_paste_keep_both(app->clipboard, d->dest_dir);
        if (win) aether_window_reload(win);
    }
    /* Cancel: لا شيء */

    app_conflict_data_free(d);
}

static void on_paste_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win || !aether_clipboard_has_content(app->clipboard)) return;
    const char *dest = aether_window_get_current_path(win);
    if (!dest) return;

    /* فحص التعارضات أولاً */
    GPtrArray *conflicts = aether_clipboard_find_conflicts(app->clipboard, dest);
    if (conflicts->len > 0) {
        /* بناء رسالة الحوار */
        GString *names_str = g_string_new("");
        guint show = MIN(conflicts->len, 5);
        for (guint i = 0; i < show; i++) {
            if (i > 0) g_string_append(names_str, "\n");
            g_string_append_printf(names_str, "• %s",
                                   (char *)g_ptr_array_index(conflicts, i));
        }
        if (conflicts->len > 5)
            g_string_append_printf(names_str, "\n… and %u more",
                                   conflicts->len - 5);

        const char *op_word = (aether_clipboard_get_op(app->clipboard)
                               == AETHER_CLIPBOARD_COPY) ? "Copying" : "Moving";
        char *msg = g_strdup_printf(
            "%s %u item%s to a location where item%s with the same name already exist%s.\n\n"
            "%s\n\nWhat would you like to do?",
            op_word,
            conflicts->len, conflicts->len > 1 ? "s" : "",
            conflicts->len > 1 ? "s" : "",
            conflicts->len > 1 ? "" : "s",
            names_str->str);
        g_string_free(names_str, TRUE);

        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(win),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
            "File Conflict");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", msg);
        g_free(msg);

        gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancel",    GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(dlg), "Keep Both", GTK_RESPONSE_YES);
        GtkWidget *rb = gtk_dialog_add_button(GTK_DIALOG(dlg), "Replace",
                                              GTK_RESPONSE_ACCEPT);
        gtk_widget_add_css_class(rb, "destructive-action");
        gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

        AppConflictData *d = g_new0(AppConflictData, 1);
        d->app            = app;
        d->dest_dir       = g_strdup(dest);
        d->conflict_names = conflicts; /* يأخذ ملكية المصفوفة */

        g_signal_connect(dlg, "response",
                         G_CALLBACK(on_app_conflict_response), d);
        gtk_window_present(GTK_WINDOW(dlg));
    } else {
        g_ptr_array_free(conflicts, TRUE);
        aether_window_start_progress(win);
        aether_clipboard_paste(app->clipboard, dest, on_paste_done, app);
    }
}

/* ── rename ── */
static void on_rename_response(GtkDialog *d, int response_id, gpointer ud) {
    (void)ud;
    if (response_id != GTK_RESPONSE_ACCEPT) {
        gtk_window_destroy(GTK_WINDOW(d));
        return;
    }
    const char *src      = (const char *)g_object_get_data(G_OBJECT(d), "src-path");
    GtkWidget  *ent      = GTK_WIDGET(g_object_get_data(G_OBJECT(d), "entry"));
    AetherApplication *a = AETHER_APPLICATION(g_object_get_data(G_OBJECT(d), "app-ref"));
    const char *new_name = gtk_editable_get_text(GTK_EDITABLE(ent));
    if (!new_name || new_name[0] == '\0') {
        gtk_window_destroy(GTK_WINDOW(d));
        return;
    }
    GFile  *file = g_file_parse_name(src);
    GError *err  = NULL;
    GFile  *renamed = g_file_set_display_name(file, new_name, NULL, &err);
    if (err) {
        /* عند فشل إعادة التسمية بسبب الصلاحيات، استخدم move عبر المساعد */
        if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
            aether_privileged_is_available()) {
            g_error_free(err);
            char *parent = g_path_get_dirname(src);
            char *new_path = g_build_filename(parent, new_name, NULL);
            AetherWindow *w2 = get_active_win(G_APPLICATION(a));
            aether_privileged_move_async(src, new_path,
                (GAsyncReadyCallback)on_privileged_op_done, w2);
            g_free(parent);
            g_free(new_path);
        } else {
            g_printerr("Rename error: %s\n", err->message);
            g_error_free(err);
        }
    } else {
        AetherWindow *w = get_active_win(G_APPLICATION(a));
        if (w) aether_window_reload(w);
    }
    if (renamed) g_object_unref(renamed);
    g_object_unref(file);
    gtk_window_destroy(GTK_WINDOW(d));
}

static void on_rename_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    
    GStrv paths = aether_window_get_selected_paths(win);
    if (!paths || !paths[0]) {
        if (paths) g_strfreev(paths);
        return;
    }
    const char *path = paths[0];

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_OTHER,
                                               GTK_BUTTONS_NONE,
                                               "Rename");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Rename", GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *entry = gtk_entry_new();
    char *basename = g_path_get_basename(path);
    gtk_editable_set_text(GTK_EDITABLE(entry), basename);
    const char *dot = strrchr(basename, '.');
    int sel_end = dot ? (int)(dot - basename) : -1;
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, sel_end);
    g_free(basename);

    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gtk_box_append(GTK_BOX(area), entry);

    g_object_set_data_full(G_OBJECT(dialog), "src-path", g_strdup(path), g_free);
    g_object_set_data(G_OBJECT(dialog), "entry",   entry);
    g_object_set_data(G_OBJECT(dialog), "app-ref", app);
    g_signal_connect(dialog, "response", G_CALLBACK(on_rename_response), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
    
    g_strfreev(paths);
}

static void on_rename_path_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    g_printerr("DEBUG: on_rename_path_action triggered.\n");
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    g_printerr("DEBUG: win is %p\n", win);
    if (!win) return;
    
    const char *path = g_variant_get_string(parameter, NULL);
    g_printerr("DEBUG: path is %s\n", path);
    if (!path) return;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_OTHER,
                                               GTK_BUTTONS_NONE,
                                               "Rename");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Rename", GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *entry = gtk_entry_new();
    char *basename = g_path_get_basename(path);
    gtk_editable_set_text(GTK_EDITABLE(entry), basename);
    const char *dot = strrchr(basename, '.');
    int sel_end = dot ? (int)(dot - basename) : -1;
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, sel_end);
    g_free(basename);

    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gtk_box_append(GTK_BOX(area), entry);

    g_object_set_data_full(G_OBJECT(dialog), "src-path", g_strdup(path), g_free);
    g_object_set_data(G_OBJECT(dialog), "entry",   entry);
    g_object_set_data(G_OBJECT(dialog), "app-ref", app);
    g_signal_connect(dialog, "response", G_CALLBACK(on_rename_response), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ── trash ── */
/* ── trash ── */
typedef struct { AetherApplication *app; char *path; } TrashCtx;

static void on_privileged_trash_done(GObject *src, GAsyncResult *res, gpointer ud) {
    (void)src;
    TrashCtx *ctx = ud;
    GError *err = NULL;
    if (!aether_privileged_op_finish(res, &err)) {
        g_printerr("Privileged trash failed: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
    }
    AetherWindow *win = get_active_win(G_APPLICATION(ctx->app));
    if (win) aether_window_reload(win);
    g_free(ctx->path);
    g_free(ctx);
}

static void on_trash_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    GStrv paths = aether_window_get_selected_paths(win);
    if (!paths) return;
    
    for (int i = 0; paths[i]; i++) {
        GFile  *f = g_file_parse_name(paths[i]);
        GError *err  = NULL;
        if (!g_file_trash(f, NULL, &err)) {
            /* عند فشل الحذف للسلة حاول الحذف المباشر عبر المساعد */
            if (err && err->code == G_IO_ERROR_PERMISSION_DENIED &&
                aether_privileged_is_available()) {
                g_error_free(err);
                TrashCtx *ctx = g_new0(TrashCtx, 1);
                ctx->app  = app;
                ctx->path = g_strdup(paths[i]);
                aether_privileged_delete_async(paths[i],
                    (GAsyncReadyCallback)on_privileged_trash_done, ctx);
            } else {
                g_printerr("Trash failed: %s\n", err ? err->message : "?");
                if (err) g_error_free(err);
            }
        } else {
            aether_window_reload(win);
        }
        g_object_unref(f);
    }
    g_strfreev(paths);
}

static void on_archive_done(GObject *source, GAsyncResult *res, gpointer user_data);


static void on_extract_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    AetherWindow *win = get_active_win(G_APPLICATION(user_data));
    if (!win) return;

    const char *archive_path = g_variant_get_string(parameter, NULL);
    if (!archive_path || strlen(archive_path) == 0) return;
    
    char *dest_dir = g_path_get_dirname(archive_path);
    
    aether_window_start_progress(win);

    /* إذا كنا في وضع الصلاحيات المرتفعة أو المسار محمي، استخدم الـ daemon مباشرة */
    if ((aether_window_get_elevated_mode(win) || !g_access(dest_dir, W_OK)) &&
        aether_privileged_is_available()) {
        if (!aether_privileged_daemon_is_running())
            aether_privileged_daemon_start();
        aether_privileged_extract_async(archive_path, dest_dir,
            (GAsyncReadyCallback)on_privileged_op_done, win);
    } else {
        aether_archive_manager_extract_async(aether_archive_manager_get_default(), 
                                             archive_path, dest_dir, 
                                             on_archive_done, win);
    }
    g_free(dest_dir);
}

/* callback موحّد للأرشيف العادي — يُعيد المحاولة عبر الـ daemon عند فشله بالصلاحيات */
typedef struct { AetherApplication *app; char *format; char *dest; GStrv sources; AetherWindow *win; } ArchiveCtx;

static void on_archive_done(GObject *source, GAsyncResult *res, gpointer user_data) {
    (void)source;
    AetherWindow *win = AETHER_WINDOW(user_data);
    GError *err = NULL;
    gboolean success = g_task_propagate_boolean(G_TASK(res), &err);
    
    if (!success && err) {
        if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
            aether_privileged_is_available()) {
            /* لا يمكن التكرار هنا مباشرة — سنسجّل فقط */
            g_printerr("Archive permission denied, use elevated mode for protected paths.\n");
        } else {
            g_printerr("Archive operation failed: %s\n", err->message);
        }
        g_error_free(err);
    }
    
    aether_window_stop_progress(win);
    aether_window_reload(win);
}

static void on_compress_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    AetherWindow *win = get_active_win(G_APPLICATION(user_data));
    if (!win) return;

    const char *format = g_variant_get_string(parameter, NULL);
    if (!format) return;
    
    GStrv paths = aether_window_get_selected_paths(win);
    if (!paths || !paths[0]) {
        g_strfreev(paths);
        return;
    }
    
    const char *current_dir = aether_window_get_current_path(win);
    if (!current_dir) {
        g_strfreev(paths);
        return;
    }
    
    char *base_name;
    if (g_strv_length(paths) == 1) {
        base_name = g_path_get_basename(paths[0]);
        char *dot = strrchr(base_name, '.');
        if (dot && dot != base_name) {
            *dot = '\0';
        }
    } else {
        base_name = g_path_get_basename(current_dir);
    }
    
    char *dest_name = g_strdup_printf("%s.%s", base_name, format);
    g_free(base_name);
    char *dest_path = g_build_filename(current_dir, dest_name, NULL);
    
    int counter = 1;
    while (g_file_test(dest_path, G_FILE_TEST_EXISTS)) {
        g_free(dest_path);
        g_free(dest_name);
        dest_name = g_strdup_printf("%s (%d).%s", base_name, counter++, format);
        dest_path = g_build_filename(current_dir, dest_name, NULL);
    }
    
    aether_window_start_progress(win);

    /* إذا كنا في وضع الصلاحيات المرتفعة أو المسار محمي، استخدم الـ daemon */
    if ((aether_window_get_elevated_mode(win) || !g_access(current_dir, W_OK)) &&
        aether_privileged_is_available()) {
        if (!aether_privileged_daemon_is_running())
            aether_privileged_daemon_start();
        aether_privileged_compress_async(format, dest_path, paths,
            (GAsyncReadyCallback)on_privileged_op_done, win);
    } else {
        aether_archive_manager_compress_async(aether_archive_manager_get_default(), 
                                              paths, dest_path, format, 
                                              on_archive_done, win);
    }
                                          
    g_free(dest_path);
    g_free(dest_name);
    g_strfreev(paths);
}

static void on_restore_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    
    GStrv paths = aether_window_get_selected_paths(win);
    if (!paths) return;
    
    for (int i = 0; paths[i]; i++) {
        GFile *file = g_file_parse_name(paths[i]);
        GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH, 
                                            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (info) {
            const char *orig_path = g_file_info_get_attribute_byte_string(info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);
            if (orig_path) {
                GFile *dest = g_file_parse_name(orig_path);
                GError *err = NULL;
                g_file_move(file, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
                if (err) {
                    /* عند فشل النقل بسبب الصلاحيات, نسخ (من السلة) إلى المسار الأصلي عبر الـ daemon */
                    if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
                        aether_privileged_is_available()) {
                        g_error_free(err);
                        /* المسار في السلة هو trash:/// وليس مسار حقيقي مباشر — نسخ تعاودي من المسار الحقيقي للسلة */
                        char *actual_src = g_file_get_path(file); /* قد يكون NULL لـ trash:// */
                        if (actual_src) {
                            aether_window_start_progress(win);
                            aether_privileged_move_async(actual_src, orig_path,
                                (GAsyncReadyCallback)on_privileged_op_done, win);
                            g_free(actual_src);
                        }
                    } else {
                        g_printerr("Restore error: %s\n", err->message);
                        g_error_free(err);
                    }
                }
                g_object_unref(dest);
            }
            g_object_unref(info);
        }
        g_object_unref(file);
    }
    g_strfreev(paths);
    aether_window_reload(win);
}

static void on_delete_permanently_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    
    GStrv paths = aether_window_get_selected_paths(win);
    if (!paths) return;
    
    for (int i = 0; paths[i]; i++) {
        GFile *file = g_file_parse_name(paths[i]);
        GError *err = NULL;
        if (!g_file_delete(file, NULL, &err)) {
            /* عند فشل الحذف بسبب الصلاحيات، حاول عبر المساعد */
            if (err && err->code == G_IO_ERROR_PERMISSION_DENIED &&
                aether_privileged_is_available()) {
                g_error_free(err);
                aether_window_start_progress(win);
                aether_privileged_delete_async(paths[i],
                    (GAsyncReadyCallback)on_privileged_op_done, win);
            } else {
                g_printerr("Delete error: %s\n", err ? err->message : "?");
                if (err) g_error_free(err);
            }
        }
        g_object_unref(file);
    }
    g_strfreev(paths);
    aether_window_reload(win);
}

static void on_empty_trash_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;

    GFile *trash = g_file_parse_name("trash:///");
    GFileEnumerator *e = g_file_enumerate_children(trash, "standard::name", G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (e) {
        GFileInfo *info;
        while ((info = g_file_enumerator_next_file(e, NULL, NULL)) != NULL) {
            GFile *child = g_file_enumerator_get_child(e, info);
            GError *err = NULL;
            if (!g_file_delete(child, NULL, &err)) {
                if (err && err->code == G_IO_ERROR_PERMISSION_DENIED &&
                    aether_privileged_is_available()) {
                    g_error_free(err);
                    char *child_path = g_file_get_path(child);
                    if (child_path) {
                        aether_privileged_delete_async(child_path,
                            (GAsyncReadyCallback)on_privileged_op_done, win);
                        g_free(child_path);
                    }
                } else if (err) {
                    g_printerr("Empty trash error: %s\n", err->message);
                    g_error_free(err);
                }
            }
            g_object_unref(child);
            g_object_unref(info);
        }
        g_object_unref(e);
    }
    g_object_unref(trash);
    aether_window_reload(win);
}

static void on_restore_all_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;

    GFile *trash = g_file_parse_name("trash:///");
    GFileEnumerator *e = g_file_enumerate_children(trash, "standard::name," G_FILE_ATTRIBUTE_TRASH_ORIG_PATH, G_FILE_QUERY_INFO_NONE, NULL, NULL);
    if (e) {
        GFileInfo *info;
        while ((info = g_file_enumerator_next_file(e, NULL, NULL)) != NULL) {
            const char *orig_path = g_file_info_get_attribute_byte_string(info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);
            if (orig_path) {
                GFile *child = g_file_enumerator_get_child(e, info);
                GFile *dest = g_file_parse_name(orig_path);
                GError *err = NULL;
                g_file_move(child, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
                if (err) {
                    if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
                        aether_privileged_is_available()) {
                        g_error_free(err);
                        char *child_path = g_file_get_path(child);
                        if (child_path) {
                            aether_window_start_progress(win);
                            aether_privileged_move_async(child_path, orig_path,
                                (GAsyncReadyCallback)on_privileged_op_done, win);
                            g_free(child_path);
                        }
                    } else {
                        g_printerr("Restore-all error: %s\n", err->message);
                        g_error_free(err);
                    }
                }
                g_object_unref(dest);
                g_object_unref(child);
            }
            g_object_unref(info);
        }
        g_object_unref(e);
    }
    g_object_unref(trash);
    aether_window_reload(win);
}

static void on_share_bluetooth_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    
    GStrv paths = aether_window_get_selected_paths(win);
    if (!paths || !paths[0]) {
        if (paths) g_strfreev(paths);
        return;
    }
    
    AetherBluetoothShareDialog *dialog = aether_bluetooth_share_dialog_new(GTK_WINDOW(win), paths);
    gtk_window_present(GTK_WINDOW(dialog));
    g_strfreev(paths);
}

/* ── properties ── */
static void on_properties_response(GtkDialog *d, int response_id, gpointer ud) {
    (void)response_id; (void)ud;
    gtk_window_destroy(GTK_WINDOW(d));
}

static void add_prop_row(GtkGrid *grid, int row, const char *key, const char *val) {
    GtkWidget *k = gtk_label_new(key);
    GtkWidget *v = gtk_label_new(val);
    gtk_widget_add_css_class(k, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(k), 1.0f);
    gtk_label_set_xalign(GTK_LABEL(v), 0.0f);
    gtk_label_set_selectable(GTK_LABEL(v), TRUE);
    gtk_grid_attach(grid, k, 0, row, 1, 1);
    gtk_grid_attach(grid, v, 1, row, 1, 1);
}

static void on_properties_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    const char *path = g_variant_get_string(parameter, NULL);

    GFile  *file = g_file_parse_name(path);
    GError *err  = NULL;
    GFileInfo *info = g_file_query_info(file,
        G_FILE_ATTRIBUTE_STANDARD_SIZE ","
        G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE ","
        G_FILE_ATTRIBUTE_TIME_MODIFIED,
        G_FILE_QUERY_INFO_NONE, NULL, &err);
    if (err) { g_error_free(err); err = NULL; }
    g_object_unref(file);

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_OTHER,
                                               GTK_BUTTONS_OK,
                                               "Properties");

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 16);
    gtk_widget_set_margin_top(grid, 8);

    int row = 0;
    char *basename = g_path_get_basename(path);
    add_prop_row(GTK_GRID(grid), row++, "Name:",     basename);
    add_prop_row(GTK_GRID(grid), row++, "Location:", path);
    g_free(basename);

    if (info) {
        goffset size = g_file_info_get_size(info);
        char *size_str;
        if (size < 1024)
            size_str = g_strdup_printf("%" G_GOFFSET_FORMAT " bytes", size);
        else if (size < 1024*1024)
            size_str = g_strdup_printf("%.1f KB", (double)size/1024.0);
        else
            size_str = g_strdup_printf("%.2f MB", (double)size/(1024.0*1024.0));
        add_prop_row(GTK_GRID(grid), row++, "Size:", size_str);
        g_free(size_str);

        const char *ct = g_file_info_get_content_type(info);
        if (ct) add_prop_row(GTK_GRID(grid), row++, "Type:", ct);

        GDateTime *mtime = g_file_info_get_modification_date_time(info);
        if (mtime) {
            char *mt = g_date_time_format(mtime, "%Y-%m-%d %H:%M");
            add_prop_row(GTK_GRID(grid), row++, "Modified:", mt);
            g_free(mt);
            g_date_time_unref(mtime);
        }
        g_object_unref(info);
    }

    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gtk_box_append(GTK_BOX(area), grid);
    g_signal_connect(dialog, "response", G_CALLBACK(on_properties_response), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
}

/* ── set_background ── */
static void on_set_background_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)user_data;
    const char *path = g_variant_get_string(parameter, NULL);
    if (!path) return;

    const char *config_dir = g_get_user_config_dir();
    char *vaxp_dir = g_build_filename(config_dir, "vaxp", NULL);
    g_mkdir_with_parents(vaxp_dir, 0755);

    char *wallpaper_file = g_build_filename(vaxp_dir, "wallpaper", NULL);
    GError *err = NULL;
    g_file_set_contents(wallpaper_file, path, -1, &err);
    if (err) {
        g_printerr("Failed to set background: %s\n", err->message);
        g_error_free(err);
    }
    g_free(wallpaper_file);
    g_free(vaxp_dir);
}

/* ── app init ── */
static void aether_application_finalize(GObject *object) {
    AetherApplication *self = AETHER_APPLICATION(object);
    aether_clipboard_controller_free(self->clipboard);
    G_OBJECT_CLASS(aether_application_parent_class)->finalize(object);
}

static void aether_application_class_init(AetherApplicationClass *klass) {
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->finalize = aether_application_finalize;
    GApplicationClass *ac = G_APPLICATION_CLASS(klass);
    ac->activate = aether_application_activate;
    ac->startup  = aether_application_startup;
}

static void aether_application_init(AetherApplication *app) {
    app->clipboard = aether_clipboard_controller_new();

    struct { const char *name; GCallback cb; const GVariantType *type; } actions[] = {
        { "open",           G_CALLBACK(on_open_action),           G_VARIANT_TYPE_STRING },
        { "cut",            G_CALLBACK(on_cut_action),            NULL },
        { "copy",           G_CALLBACK(on_copy_action),           NULL },
        { "paste",          G_CALLBACK(on_paste_action),          NULL },
        { "rename",         G_CALLBACK(on_rename_action),         NULL },
        { "rename-path",    G_CALLBACK(on_rename_path_action),    G_VARIANT_TYPE_STRING },
        { "trash",          G_CALLBACK(on_trash_action),          NULL },
        { "restore",        G_CALLBACK(on_restore_action),        NULL },
        { "delete-permanently", G_CALLBACK(on_delete_permanently_action), NULL },
        { "empty-trash",    G_CALLBACK(on_empty_trash_action),    NULL },
        { "restore-all",    G_CALLBACK(on_restore_all_action),    NULL },
        { "share-bluetooth",G_CALLBACK(on_share_bluetooth_action),NULL },
        { "properties",     G_CALLBACK(on_properties_action),     G_VARIANT_TYPE_STRING },
        { "set_background", G_CALLBACK(on_set_background_action), G_VARIANT_TYPE_STRING },
        { "extract",        G_CALLBACK(on_extract_action),        G_VARIANT_TYPE_STRING },
        { "compress",       G_CALLBACK(on_compress_action),       G_VARIANT_TYPE_STRING },
        { NULL, NULL, NULL }
    };

    for (int i = 0; actions[i].name; i++) {
        GSimpleAction *a = g_simple_action_new(actions[i].name, actions[i].type);
        g_signal_connect(a, "activate", actions[i].cb, app);
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
        g_object_unref(a);
    }

    const char *accels_cut[] = { "<Ctrl>x", NULL };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.cut", accels_cut);

    const char *accels_copy[] = { "<Ctrl>c", NULL };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.copy", accels_copy);

    const char *accels_paste[] = { "<Ctrl>v", NULL };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.paste", accels_paste);

    const char *accels_rename[] = { "F2", NULL };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.rename", accels_rename);
    
    const char *accels_trash[] = { "Delete", NULL };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.trash", accels_trash);
}

AetherApplication *aether_application_new(void) {
    return g_object_new(AETHER_TYPE_APPLICATION,
                        "application-id", "com.aetheros.files",
                        "flags", G_APPLICATION_DEFAULT_FLAGS,
                        NULL);
}
