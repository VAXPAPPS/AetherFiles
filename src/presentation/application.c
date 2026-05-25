#include "application.h"
#include "views/window.h"
#include "controllers/clipboard_controller.h"
#include <gio/gio.h>
#include <string.h>

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
static void on_paste_done(GObject *src, GAsyncResult *res, gpointer ud) {
    (void)src;
    AetherApplication *app = AETHER_APPLICATION(ud);
    GError *err = NULL;
    aether_clipboard_paste_finish(app->clipboard, res, &err);
    if (err) { g_printerr("Paste error: %s\n", err->message); g_error_free(err); }
    AetherWindow *w = get_active_win(G_APPLICATION(app));
    if (w) aether_window_reload(w);
}

static void on_paste_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win || !aether_clipboard_has_content(app->clipboard)) return;
    const char *dest = aether_window_get_current_path(win);
    if (!dest) return;
    aether_clipboard_paste(app->clipboard, dest, on_paste_done, app);
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
    GFile  *file = g_file_new_for_path(src);
    GError *err  = NULL;
    GFile  *renamed = g_file_set_display_name(file, new_name, NULL, &err);
    if (err) { g_printerr("Rename error: %s\n", err->message); g_error_free(err); }
    if (renamed) g_object_unref(renamed);
    g_object_unref(file);
    AetherWindow *w = get_active_win(G_APPLICATION(a));
    if (w) aether_window_reload(w);
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
static void on_trash_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    GStrv paths = aether_window_get_selected_paths(win);
    if (!paths) return;
    
    for (int i = 0; paths[i]; i++) {
        GFile  *file = g_file_new_for_path(paths[i]);
        GError *err  = NULL;
        g_file_trash(file, NULL, &err);
        if (err) { g_printerr("Trash error: %s\n", err->message); g_error_free(err); }
        g_object_unref(file);
    }
    g_strfreev(paths);
    aether_window_reload(win);
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

    GFile  *file = g_file_new_for_path(path);
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
    char *cmd = g_strdup_printf("vaxp-setbg \"%s\"", path);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
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
        { "properties",     G_CALLBACK(on_properties_action),     G_VARIANT_TYPE_STRING },
        { "set_background", G_CALLBACK(on_set_background_action), G_VARIANT_TYPE_STRING },
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
