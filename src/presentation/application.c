#include "application.h"
#include "views/window.h"
#include "controllers/clipboard_controller.h"
#include <gio/gio.h>
#include <string.h>

struct _AetherApplication {
    AdwApplication parent_instance;
    AetherClipboardController *clipboard;
};

G_DEFINE_TYPE(AetherApplication, aether_application, ADW_TYPE_APPLICATION)

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
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(),
                                       ADW_COLOR_SCHEME_PREFER_DARK);
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
    (void)action;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    const char *path = g_variant_get_string(parameter, NULL);
    aether_clipboard_set(app->clipboard, path, AETHER_CLIPBOARD_CUT);
}

static void on_copy_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    const char *path = g_variant_get_string(parameter, NULL);
    aether_clipboard_set(app->clipboard, path, AETHER_CLIPBOARD_COPY);
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
static void on_rename_response(AdwAlertDialog *d, const char *resp, gpointer ud) {
    (void)ud;
    if (g_strcmp0(resp, "rename") != 0) return;
    const char *src      = (const char *)g_object_get_data(G_OBJECT(d), "src-path");
    GtkWidget  *ent      = GTK_WIDGET(g_object_get_data(G_OBJECT(d), "entry"));
    AetherApplication *a = AETHER_APPLICATION(g_object_get_data(G_OBJECT(d), "app-ref"));
    const char *new_name = gtk_editable_get_text(GTK_EDITABLE(ent));
    if (!new_name || new_name[0] == '\0') return;
    GFile  *file = g_file_new_for_path(src);
    GError *err  = NULL;
    GFile  *renamed = g_file_set_display_name(file, new_name, NULL, &err);
    if (err) { g_printerr("Rename error: %s\n", err->message); g_error_free(err); }
    if (renamed) g_object_unref(renamed);
    g_object_unref(file);
    AetherWindow *w = get_active_win(G_APPLICATION(a));
    if (w) aether_window_reload(w);
}

static void on_rename_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    AetherWindow *win = get_active_win(G_APPLICATION(app));
    if (!win) return;
    const char *path = g_variant_get_string(parameter, NULL);

    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Rename", NULL));
    GtkWidget *entry = gtk_entry_new();
    char *basename = g_path_get_basename(path);
    gtk_editable_set_text(GTK_EDITABLE(entry), basename);
    const char *dot = strrchr(basename, '.');
    int sel_end = dot ? (int)(dot - basename) : -1;
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, sel_end);
    g_free(basename);

    adw_alert_dialog_set_extra_child(dialog, entry);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog),
                                   "cancel", "Cancel", "rename", "Rename", NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog),
                                              "rename", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "rename");
    g_object_set_data_full(G_OBJECT(dialog), "src-path", g_strdup(path), g_free);
    g_object_set_data(G_OBJECT(dialog), "entry",   entry);
    g_object_set_data(G_OBJECT(dialog), "app-ref", app);
    g_signal_connect(dialog, "response", G_CALLBACK(on_rename_response), NULL);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(win));
}

/* ── trash ── */
static void on_trash_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action;
    AetherApplication *app = AETHER_APPLICATION(user_data);
    const char *path = g_variant_get_string(parameter, NULL);
    GFile  *file = g_file_new_for_path(path);
    GError *err  = NULL;
    g_file_trash(file, NULL, &err);
    if (err) { g_printerr("Trash error: %s\n", err->message); g_error_free(err); }
    g_object_unref(file);
    AetherWindow *w = get_active_win(G_APPLICATION(app));
    if (w) aether_window_reload(w);
}

/* ── properties ── */
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

    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Properties", NULL));
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog), "ok", "OK", NULL);

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

    adw_alert_dialog_set_extra_child(dialog, grid);
    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(win));
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

    struct { const char *name; GCallback cb; } actions[] = {
        { "open",           G_CALLBACK(on_open_action)           },
        { "cut",            G_CALLBACK(on_cut_action)            },
        { "copy",           G_CALLBACK(on_copy_action)           },
        { "paste",          G_CALLBACK(on_paste_action)          },
        { "rename",         G_CALLBACK(on_rename_action)         },
        { "trash",          G_CALLBACK(on_trash_action)          },
        { "properties",     G_CALLBACK(on_properties_action)     },
        { "set_background", G_CALLBACK(on_set_background_action) },
        { NULL, NULL }
    };

    for (int i = 0; actions[i].name; i++) {
        GSimpleAction *a = g_simple_action_new(actions[i].name, G_VARIANT_TYPE_STRING);
        g_signal_connect(a, "activate", actions[i].cb, app);
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(a));
        g_object_unref(a);
    }
}

AetherApplication *aether_application_new(void) {
    return g_object_new(AETHER_TYPE_APPLICATION,
                        "application-id", "com.aetheros.files",
                        "flags", G_APPLICATION_DEFAULT_FLAGS,
                        NULL);
}
