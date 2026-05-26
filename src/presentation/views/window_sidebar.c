#include "window_private.h"
#include <glib/gi18n.h>
#include "../../domain/drive_entity.h"

#define BOOKMARKS_FILE ".config/aetherfiles-bookmarks"

GtkWidget *make_sidebar_row(const char *name, const char *icon_name);

void load_bookmarks(AetherWindow *self) {
    /* Remove old bookmark rows first */
    GtkWidget *child = gtk_widget_get_first_child(self->sidebar_list);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        if (g_object_get_data(G_OBJECT(child), "is-bookmark")) {
            gtk_list_box_remove(GTK_LIST_BOX(self->sidebar_list), child);
        }
        child = next;
    }

    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *contents = NULL;
    if (!g_file_get_contents(bm_path, &contents, NULL, NULL)) {
        g_free(bm_path);
        return;
    }
    g_free(bm_path);

    char **lines = g_strsplit(contents, "\n", -1);
    g_free(contents);

    for (int i = 0; lines[i] != NULL; i++) {
        const char *line = lines[i];
        if (!line || line[0] == '\0') continue;
        /* Each line: file:///path [label] */
        char **parts = g_strsplit(line, " ", 2);
        const char *uri = parts[0];
        GFile *f = g_file_new_for_uri(uri);
        char  *path = g_file_get_path(f);
        g_object_unref(f);

        if (!path) { g_strfreev(parts); continue; }

        const char *label = (parts[1] && parts[1][0]) ?
                             parts[1] : g_path_get_basename(path);

        GtkWidget *bm_row = make_sidebar_row(label, "bookmark-new-symbolic");
        g_object_set_data(G_OBJECT(bm_row), "is-bookmark", GINT_TO_POINTER(1));
        g_object_set_data_full(G_OBJECT(bm_row), "path", g_strdup(path), g_free);
        gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), bm_row);

        g_free(path);
        g_strfreev(parts);
    }
    g_strfreev(lines);
}

void save_bookmark(const char *path) {
    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *existing = NULL;
    g_file_get_contents(bm_path, &existing, NULL, NULL);

    char *uri     = g_filename_to_uri(path, NULL, NULL);
    char *new_line = g_strdup_printf("%s\n", uri);
    char *new_contents;
    if (existing)
        new_contents = g_strconcat(existing, new_line, NULL);
    else
        new_contents = g_strdup(new_line);

    g_file_set_contents(bm_path, new_contents, -1, NULL);
    g_free(existing); g_free(uri); g_free(new_line);
    g_free(new_contents); g_free(bm_path);
}

void remove_bookmark(const char *path) {
    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *existing = NULL;
    if (!g_file_get_contents(bm_path, &existing, NULL, NULL)) {
        g_free(bm_path);
        return;
    }

    char *uri = g_filename_to_uri(path, NULL, NULL);
    char **lines = g_strsplit(existing, "\n", -1);
    GString *new_contents = g_string_new("");

    for (int i = 0; lines[i]; i++) {
        if (lines[i][0] == '\0') continue;
        char **parts = g_strsplit(lines[i], " ", 2);
        if (g_strcmp0(parts[0], uri) != 0) {
            g_string_append_printf(new_contents, "%s\n", lines[i]);
        }
        g_strfreev(parts);
    }

    g_file_set_contents(bm_path, new_contents->str, new_contents->len, NULL);

    g_string_free(new_contents, TRUE);
    g_strfreev(lines);
    g_free(uri);
    g_free(existing);
    g_free(bm_path);
}

gboolean is_bookmarked(const char *path) {
    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *existing = NULL;
    if (!g_file_get_contents(bm_path, &existing, NULL, NULL)) {
        g_free(bm_path);
        return FALSE;
    }

    char *uri = g_filename_to_uri(path, NULL, NULL);
    char **lines = g_strsplit(existing, "\n", -1);
    gboolean found = FALSE;

    for (int i = 0; lines[i]; i++) {
        if (lines[i][0] == '\0') continue;
        char **parts = g_strsplit(lines[i], " ", 2);
        if (g_strcmp0(parts[0], uri) == 0) {
            found = TRUE;
        }
        g_strfreev(parts);
        if (found) break;
    }

    g_strfreev(lines);
    g_free(uri);
    g_free(existing);
    g_free(bm_path);
    return found;
}

static gboolean on_sidebar_row_drop_accept(GtkDropTarget *target, GdkDrop *drop, gpointer user_data) {
    (void)target; (void)drop;
    GtkWidget *row = GTK_WIDGET(user_data);
    const char *dest_path = g_object_get_data(G_OBJECT(row), "path");
    return dest_path != NULL;
}

typedef struct {
    GPtrArray *deb_paths;
    AetherWindow *win;
} InstallData;

static void on_install_process_exited(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    InstallData *data = user_data;
    GSubprocess *proc = G_SUBPROCESS(source_object);
    GError *err = NULL;
    g_subprocess_wait_finish(proc, res, &err);
    if (err) {
        g_printerr("Install failed: %s\n", err->message);
        g_error_free(err);
    } else {
        if (g_subprocess_get_if_exited(proc) && g_subprocess_get_exit_status(proc) == 0) {
            g_print("Installation successful!\n");
            if (data && data->win && data->win->app_repo) {
                aether_app_repository_load_apps(data->win->app_repo);
            }
        } else {
            g_printerr("Installation failed or exited with error.\n");
        }
    }
    
    if (data) {
        g_ptr_array_free(data->deb_paths, TRUE);
        g_free(data);
    }
    g_object_unref(proc);
}

static void on_install_password_submit(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    GtkWidget *dialog = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);
    GtkWidget *entry = g_object_get_data(G_OBJECT(dialog), "pwd-entry");
    InstallData *data = g_object_get_data(G_OBJECT(dialog), "install-data");
    
    const char *pwd = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!pwd || strlen(pwd) == 0) return;
    
    GPtrArray *cmd = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(cmd, g_strdup("sudo"));
    g_ptr_array_add(cmd, g_strdup("-S"));
    g_ptr_array_add(cmd, g_strdup("apt-get"));
    g_ptr_array_add(cmd, g_strdup("install"));
    g_ptr_array_add(cmd, g_strdup("-y"));
    for (guint i = 0; i < data->deb_paths->len; i++) {
        g_ptr_array_add(cmd, g_strdup(g_ptr_array_index(data->deb_paths, i)));
    }
    g_ptr_array_add(cmd, NULL);
    
    GError *err = NULL;
    GSubprocess *proc = g_subprocess_newv((const char * const *)cmd->pdata,
        G_SUBPROCESS_FLAGS_STDIN_PIPE | G_SUBPROCESS_FLAGS_STDOUT_PIPE | G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &err);
        
    if (proc) {
        char *pwd_nl = g_strdup_printf("%s\n", pwd);
        GOutputStream *stdin_stream = g_subprocess_get_stdin_pipe(proc);
        g_output_stream_write_all(stdin_stream, pwd_nl, strlen(pwd_nl), NULL, NULL, NULL);
        g_output_stream_close(stdin_stream, NULL, NULL);
        g_free(pwd_nl);
        
        g_subprocess_wait_async(proc, NULL, on_install_process_exited, data);
    } else {
        g_printerr("Failed to spawn sudo: %s\n", err->message);
        g_error_free(err);
        g_ptr_array_free(data->deb_paths, TRUE);
        g_free(data);
    }
    
    g_ptr_array_free(cmd, TRUE);
    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void show_install_auth_dialog(AetherWindow *win, GPtrArray *deb_paths) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Authentication Required");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 300, 150);
    
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_data(provider, "window { background-color: rgba(0, 0, 0, 0.3); }", -1);
    gtk_style_context_add_provider(gtk_widget_get_style_context(dialog), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_start(box, 16);
    gtk_widget_set_margin_end(box, 16);
    gtk_widget_set_margin_top(box, 16);
    gtk_widget_set_margin_bottom(box, 16);
    
    GtkWidget *lbl = gtk_label_new("Enter administrator password to install packages:");
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    
    GtkWidget *entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE);
    
    GtkWidget *btn = gtk_button_new_with_label("Install");
    gtk_widget_add_css_class(btn, "suggested-action");
    
    InstallData *data = g_new0(InstallData, 1);
    data->deb_paths = deb_paths;
    data->win = win;
    
    g_object_set_data(G_OBJECT(dialog), "pwd-entry", entry);
    g_object_set_data(G_OBJECT(dialog), "install-data", data);
    
    g_signal_connect(btn, "clicked", G_CALLBACK(on_install_password_submit), NULL);
    g_signal_connect(entry, "activate", G_CALLBACK(on_install_password_submit), NULL);
    
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), entry);
    gtk_box_append(GTK_BOX(box), btn);
    
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_window_present(GTK_WINDOW(dialog));
}

static gboolean on_sidebar_row_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data) {
    (void)target; (void)x; (void)y;
    GtkWidget *row = GTK_WIDGET(user_data);
    const char *dest_path = g_object_get_data(G_OBJECT(row), "path");
    
    if (!dest_path) return FALSE;
    
    GdkFileList *file_list = g_value_get_boxed(value);
    if (!file_list) return FALSE;
    
    GSList *files = gdk_file_list_get_files(file_list);
    GFile *dest_dir = g_file_new_for_path(dest_path);
    gboolean success = TRUE;
    
    gboolean is_trash = g_strcmp0(dest_path, "trash:///") == 0;
    gboolean is_apps  = g_strcmp0(dest_path, "apps:///") == 0;
    
    if (is_apps) {
        GPtrArray *deb_paths = g_ptr_array_new_with_free_func(g_free);
        for (GSList *l = files; l != NULL; l = l->next) {
            GFile *src = G_FILE(l->data);
            char *path = g_file_get_path(src);
            if (path && g_str_has_suffix(path, ".deb")) {
                g_ptr_array_add(deb_paths, path);
            } else {
                g_free(path);
            }
        }
        
        if (deb_paths->len > 0) {
            GtkWidget *win_widget = gtk_widget_get_ancestor(row, AETHER_TYPE_WINDOW);
            if (win_widget) {
                show_install_auth_dialog(AETHER_WINDOW(win_widget), deb_paths);
            } else {
                g_ptr_array_free(deb_paths, TRUE);
            }
        } else {
            g_ptr_array_free(deb_paths, TRUE);
        }
        
        g_object_unref(dest_dir);
        g_slist_free(files);
        return success;
    }
    
    for (GSList *l = files; l != NULL; l = l->next) {
        GFile *src = G_FILE(l->data);
        GError *err = NULL;
        
        if (is_trash) {
            g_file_trash(src, NULL, &err);
        } else {
            char *basename = g_file_get_basename(src);
            GFile *dest_file = g_file_get_child(dest_dir, basename);
            g_file_move(src, dest_file, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
            g_object_unref(dest_file);
            g_free(basename);
        }
        
        if (err) {
            g_printerr("Sidebar DnD error: Error moving file %s: %s\n", dest_path, err->message);
            g_error_free(err);
            success = FALSE;
        }
    }
    g_object_unref(dest_dir);
    g_slist_free(files);
    
    GtkWidget *win_widget = gtk_widget_get_ancestor(row, AETHER_TYPE_WINDOW);
    if (win_widget) {
        aether_window_reload(AETHER_WINDOW(win_widget));
    }
    
    return success;
}

GtkWidget *make_sidebar_row(const char *name, const char *icon_name) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget *icon  = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
    GtkWidget *label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    
    GtkDropTarget *row_drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(row_drop, "accept", G_CALLBACK(on_sidebar_row_drop_accept), row);
    g_signal_connect(row_drop, "drop", G_CALLBACK(on_sidebar_row_drop), row);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(row_drop));
    
    return row;
}

void add_sidebar_separator(AetherWindow *self) {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_widget_set_margin_top(sep, 0);
    gtk_widget_set_margin_bottom(sep, 0);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), sep);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

void add_sidebar_header(AetherWindow *self, const char *title) {
    GtkWidget *lbl = gtk_label_new(title);
    gtk_widget_add_css_class(lbl, "sidebar-section-label");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_sensitive(lbl, FALSE);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

void add_sidebar_place(AetherWindow *self, const char *name,
                               const char *icon, const char *path)
{
    GtkWidget *row = make_sidebar_row(name, icon);
    g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path), g_free);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

static void refresh_devices_sidebar(AetherDriveManager *mgr, AetherWindow *self) {
    (void)mgr;
    /* Remove old device rows */
    GtkWidget *child = gtk_widget_get_first_child(self->sidebar_list);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        if (g_object_get_data(G_OBJECT(child), "is-device")) {
            gtk_list_box_remove(GTK_LIST_BOX(self->sidebar_list), child);
        }
        child = next;
    }

    GListStore *drives = aether_drive_manager_get_drives(self->drive_mgr);
    guint n = g_list_model_get_n_items(G_LIST_MODEL(drives));
    for (guint i = 0; i < n; i++) {
        AetherDriveEntity *entity = AETHER_DRIVE_ENTITY(g_list_model_get_item(G_LIST_MODEL(drives), i));
        
        GtkWidget *row = make_sidebar_row(aether_drive_entity_get_name(entity), aether_drive_entity_get_icon_name(entity));
        g_object_set_data(G_OBJECT(row), "is-device", GINT_TO_POINTER(1));
        
        if (aether_drive_entity_is_mounted(entity)) {
            g_object_set_data_full(G_OBJECT(row), "path", g_strdup(aether_drive_entity_get_path(entity)), g_free);
        } else {
            GVolume *vol = aether_drive_entity_get_volume(entity);
            if (vol) g_object_set_data_full(G_OBJECT(row), "volume", g_object_ref(vol), g_object_unref);
        }
        
        gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
        g_object_unref(entity);
    }
}

void setup_sidebar(AetherWindow *self) {
    self->sidebar_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->sidebar_list), GTK_SELECTION_SINGLE);
    g_signal_connect(self->sidebar_list, "row-activated",
                     G_CALLBACK(on_sidebar_row_activated), self);
    gtk_widget_add_css_class(self->sidebar_list, "navigation-sidebar");

    /* Pinned */
    add_sidebar_place(self, "Recent",    "document-open-recent-symbolic", g_get_home_dir());
    add_sidebar_place(self, "Starred",   "starred-symbolic",              g_get_home_dir());
    add_sidebar_place(self, "Home",      "user-home-symbolic",            g_get_home_dir());

    char *work_path = g_build_filename(g_get_home_dir(), "Work", NULL);
    GFile *work_dir_file = g_file_new_for_path(work_path);
    if (!g_file_query_exists(work_dir_file, NULL)) {
        g_file_make_directory_with_parents(work_dir_file, NULL, NULL);
    }
    g_object_unref(work_dir_file);
    add_sidebar_place(self, "Work",      "folder-development-symbolic",   work_path);
    g_free(work_path);

    add_sidebar_place(self, "Apps",      "application-x-executable-symbolic", "apps:///");


    /* Places */
    add_sidebar_place(self, "Desktop",   "user-desktop-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP));
    add_sidebar_place(self, "Documents", "folder-documents-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS));
    add_sidebar_place(self, "Downloads", "folder-download-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD));
    add_sidebar_place(self, "Music",     "folder-music-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_MUSIC));
    add_sidebar_place(self, "Pictures",  "folder-pictures-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_PICTURES));
    add_sidebar_place(self, "Videos",    "folder-videos-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS));
    add_sidebar_place(self, "Trash",     "user-trash-symbolic",           "trash:///");


    /* Drives */

    g_signal_connect(self->drive_mgr, "drives-changed", G_CALLBACK(refresh_devices_sidebar), self);
    refresh_devices_sidebar(self->drive_mgr, self);
}

static void on_mount_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GError *err = NULL;
    if (aether_drive_manager_mount_finish(self->drive_mgr, res, &err)) {
        /* Success, the drives-changed signal will fire and update the sidebar.
           We could try to navigate to it, but finding the path here is tricky without the entity.
           Let's just let it mount for now. */
    } else {
        g_printerr("Failed to mount: %s\n", err->message);
        g_error_free(err);
    }
}

void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(row), "path");
    GVolume *vol = g_object_get_data(G_OBJECT(row), "volume");
    
    if (path) {
        if (g_strcmp0(path, "apps:///") == 0) {
            show_apps_view(self);
        } else {
            /* If we navigate to a real path, show grid or list view again */
            if (self->view_stack) {
                if (gtk_widget_has_css_class(self->btn_grid, "active-view")) {
                    gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), "grid");
                } else {
                    gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), "list");
                }
                gtk_widget_set_visible(self->path_bar, TRUE);
                gtk_widget_set_visible(self->sort_btn, TRUE);
                gtk_widget_set_visible(self->btn_grid, TRUE);
                gtk_widget_set_visible(self->btn_list, TRUE);
            }
            load_directory(self, path);
        }
    } else if (vol) {
        aether_drive_manager_mount_async(self->drive_mgr, vol, NULL, on_mount_ready, self);
    }
}

