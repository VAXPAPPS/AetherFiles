#include "window_private.h"
#include <glib/gi18n.h>
#ifdef G_OS_UNIX
#include <gio/gdesktopappinfo.h>
#endif

void load_directory(AetherWindow *self, const char *path) {
    if (self->current_path)
        g_ptr_array_add(self->back_stack, g_strdup(self->current_path));
    if (self->back_stack->len > HISTORY_MAX)
        g_ptr_array_remove_index(self->back_stack, 0);

    /* Clear forward stack on new navigation */
    g_ptr_array_set_size(self->fwd_stack, 0);

    g_free(self->current_path);
    self->current_path = g_strdup(path);
    update_pathbar(self);
    update_nav_buttons(self);
    aether_file_repository_list_directory_async(
        self->repo, self->current_path, NULL, on_directory_loaded, self);
}

void update_nav_buttons(AetherWindow *self) {
    gtk_widget_set_sensitive(self->btn_back, self->back_stack->len > 0);
    gtk_widget_set_sensitive(self->btn_fwd,  self->fwd_stack->len  > 0);
}

void update_statusbar(AetherWindow *self) {
    if (self->filter_model) {
        self->item_count = g_list_model_get_n_items(G_LIST_MODEL(self->filter_model));
    }
    char *text = g_strdup_printf("%u items", self->item_count);
    gtk_label_set_text(GTK_LABEL(self->status_label), text);
    g_free(text);

    GFile    *f     = g_file_parse_name(self->current_path ? self->current_path : "/");
    GFileInfo *info = g_file_query_filesystem_info(f,
                          G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
                          NULL, NULL);
    g_object_unref(f);
    if (info) {
        guint64 free_bytes = g_file_info_get_attribute_uint64(info,
                                 G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
        g_object_unref(info);
        double free_gb = (double)free_bytes / (1024.0 * 1024.0 * 1024.0);
        char *space_text = g_strdup_printf("%.1f GB available", free_gb);
        gtk_label_set_text(GTK_LABEL(self->space_label), space_text);
        g_free(space_text);
    }
}

void update_pathbar(AetherWindow *self) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->path_bar)) != NULL)
        gtk_box_remove(GTK_BOX(self->path_bar), child);

    if (!self->current_path) return;

    GFile *f = g_file_parse_name(self->current_path);
    GPtrArray *parents = g_ptr_array_new_with_free_func(g_object_unref);
    
    GFile *parent = g_object_ref(f);
    while (parent != NULL) {
        g_ptr_array_add(parents, parent);
        parent = g_file_get_parent(parent);
    }
    
    for (int i = parents->len - 1; i >= 0; i--) {
        GFile *curr = G_FILE(g_ptr_array_index(parents, i));
        char *name = g_file_get_basename(curr);
        char *path = g_file_get_parse_name(curr);
        char *uri = g_file_get_uri(curr);
        
        const char *label = name;
        if (!label || strlen(label) == 0 || g_strcmp0(label, "/") == 0) {
            if (g_str_has_prefix(uri, "trash:")) label = "Trash";
            else label = "Root";
        }
        
        GtkWidget *btn = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(btn, "flat");
        g_object_set_data_full(G_OBJECT(btn), "path", g_strdup(path), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_path_button_clicked), self);
        gtk_box_append(GTK_BOX(self->path_bar), btn);
        
        if (i > 0) {
            GtkWidget *sep = gtk_label_new("›");
            gtk_widget_add_css_class(sep, "path-sep");
            gtk_widget_set_margin_start(sep, 2);
            gtk_widget_set_margin_end(sep, 2);
            gtk_box_append(GTK_BOX(self->path_bar), sep);
        }
        
        g_free(name);
        g_free(path);
        g_free(uri);
    }
    
    g_ptr_array_free(parents, TRUE);
    g_object_unref(f);
}

typedef struct {
    AetherWindow *window;
    GtkMultiSelection *grid_sel;
    GtkMultiSelection *list_sel;
    GtkFilterListModel *filter_model;
    GtkCustomFilter *name_filter;
    GtkCustomSorter *sorter;
} ModelUpdateData;

static gboolean set_model_idle(gpointer user_data) {
    ModelUpdateData *d = user_data;
    
    if (d->window->filter_model) {
        g_object_unref(d->window->filter_model);
    }
    
    gtk_grid_view_set_model(GTK_GRID_VIEW(d->window->grid_view), GTK_SELECTION_MODEL(d->grid_sel));
    gtk_column_view_set_model(GTK_COLUMN_VIEW(d->window->list_view), GTK_SELECTION_MODEL(d->list_sel));
    
    d->window->filter_model = d->filter_model;
    d->window->name_filter  = d->name_filter;
    d->window->sorter       = d->sorter;
    
    update_statusbar(d->window);
    
    g_object_unref(d->grid_sel);
    g_object_unref(d->list_sel);
    g_free(d);
    return G_SOURCE_REMOVE;
}

void on_directory_loaded(GObject *source, GAsyncResult *res, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GError *err = NULL;
    GListModel *raw = aether_file_repository_list_directory_finish(
                          AETHER_FILE_REPOSITORY(source), res, &err);
    if (err) { g_printerr("Error: %s\n", err->message); g_error_free(err); return; }

    GListStore *visible = g_list_store_new(AETHER_TYPE_FILE_ENTITY);
    guint n = g_list_model_get_n_items(raw);
    for (guint i = 0; i < n; i++) {
        AetherFileEntity *e = AETHER_FILE_ENTITY(g_list_model_get_item(raw, i));
        g_list_store_append(visible, e);
        g_object_unref(e);
    }
    g_printerr("Loaded %u items\n", n);
    g_object_unref(raw);

    /* Sort with self context so sort_mode/sort_asc are used */
    GtkCustomSorter    *sorter   = gtk_custom_sorter_new(compare_entities, self, NULL);
    GtkSortListModel   *sorted   = gtk_sort_list_model_new(G_LIST_MODEL(visible), GTK_SORTER(sorter));
    GtkCustomFilter    *filter   = gtk_custom_filter_new(name_filter_func, self, NULL);
    GtkFilterListModel *filtered = gtk_filter_list_model_new(G_LIST_MODEL(sorted), GTK_FILTER(filter));

    GtkMultiSelection *grid_sel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(filtered)));
    GtkMultiSelection *list_sel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(filtered)));
    
    ModelUpdateData *d = g_new0(ModelUpdateData, 1);
    d->window = self;
    d->grid_sel = grid_sel;
    d->list_sel = list_sel;
    d->filter_model = filtered;
    d->name_filter = filter;
    d->sorter = sorter;
    
    g_idle_add(set_model_idle, d);

    /* Start file monitor for current directory */
    setup_file_monitor(self, self->current_path);
}

void setup_file_monitor(AetherWindow *self, const char *path) {
    if (self->dir_monitor) {
        g_file_monitor_cancel(self->dir_monitor);
        g_clear_object(&self->dir_monitor);
    }
    if (!path) return;
    GFile *dir = g_file_parse_name(path);
    GError *err = NULL;
    self->dir_monitor = g_file_monitor_directory(dir,
                            G_FILE_MONITOR_WATCH_MOVES, NULL, &err);
    g_object_unref(dir);
    if (err) { g_error_free(err); return; }
    g_signal_connect(self->dir_monitor, "changed",
                     G_CALLBACK(on_dir_changed), self);
}

void navigate_back(AetherWindow *self) {
    if (self->back_stack->len == 0) return;
    if (self->current_path)
        g_ptr_array_add(self->fwd_stack, g_strdup(self->current_path));

    char *prev = g_ptr_array_steal_index(self->back_stack, self->back_stack->len - 1);
    g_free(self->current_path);
    self->current_path = prev;
    update_pathbar(self);
    update_nav_buttons(self);
    aether_file_repository_list_directory_async(
        self->repo, self->current_path, NULL, on_directory_loaded, self);
}

void navigate_forward(AetherWindow *self) {
    if (self->fwd_stack->len == 0) return;
    if (self->current_path)
        g_ptr_array_add(self->back_stack, g_strdup(self->current_path));

    char *next = g_ptr_array_steal_index(self->fwd_stack, self->fwd_stack->len - 1);
    g_free(self->current_path);
    self->current_path = next;
    update_pathbar(self);
    update_nav_buttons(self);
    aether_file_repository_list_directory_async(
        self->repo, self->current_path, NULL, on_directory_loaded, self);
}

void on_dir_changed(GFileMonitor *mon, GFile *file, GFile *other,
                           GFileMonitorEvent event, gpointer user_data)
{
    (void)mon; (void)file; (void)other;
    AetherWindow *self = AETHER_WINDOW(user_data);
    /* Debounce: only react to meaningful events */
    switch (event) {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_RENAMED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
        if (self->current_path)
            aether_file_repository_list_directory_async(
                self->repo, self->current_path, NULL, on_directory_loaded, self);
        break;
    default:
        break;
    }
}

void on_path_button_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(btn), "path");
    if (path) load_directory(self, path);
}

void on_item_activated(GtkWidget *view, guint position, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GtkSelectionModel *sel = NULL;
    if (GTK_IS_GRID_VIEW(view))
        sel = gtk_grid_view_get_model(GTK_GRID_VIEW(view));
    else if (GTK_IS_COLUMN_VIEW(view))
        sel = gtk_column_view_get_model(GTK_COLUMN_VIEW(view));
    if (!sel) return;

    gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), position);
    if (!item) return;
    AetherFileEntity *e = AETHER_FILE_ENTITY(item);
    if (aether_file_entity_is_directory(e)) {
        load_directory(self, aether_file_entity_get_path(e));
    } else {
        const char *path = aether_file_entity_get_path(e);
        const char *uri = aether_file_entity_get_uri(e);
        gboolean executed = FALSE;

        if (path && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE)) {
            GFile *file = g_file_new_for_path(path);
            GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                                G_FILE_QUERY_INFO_NONE, NULL, NULL);
            if (info) {
                const char *ctype = g_file_info_get_content_type(info);
                if (ctype) {
                    if (g_content_type_is_a(ctype, "application/x-executable") ||
                        g_content_type_is_a(ctype, "application/x-shellscript") ||
                        g_content_type_is_a(ctype, "application/x-sharedlib") ||
                        g_content_type_is_a(ctype, "application/vnd.appimage") ||
                        g_str_has_suffix(path, ".AppImage") ||
                        g_str_has_suffix(path, ".sh") ||
                        g_str_has_suffix(path, ".run")) {
                        
                        GError *err = NULL;
                        char *dir = g_path_get_dirname(path);
                        char *argv[] = { (char *)path, NULL };
                        if (g_spawn_async(dir, argv, NULL,
                                          G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)) {
                            executed = TRUE;
                        } else {
                            g_printerr("Failed to execute application: %s\n", err->message);
                            g_error_free(err);
                        }
                        g_free(dir);
                    }
                }
                g_object_unref(info);
            }
            g_object_unref(file);
        }

#ifdef G_OS_UNIX
        /* Special handling for .desktop files */
        if (!executed && path && g_str_has_suffix(path, ".desktop")) {
            GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(path);
            if (app_info) {
                GError *err = NULL;
                if (g_app_info_launch(G_APP_INFO(app_info), NULL, NULL, &err)) {
                    executed = TRUE;
                } else {
                    g_printerr("Failed to launch desktop file: %s\n", err->message);
                    g_error_free(err);
                }
                g_object_unref(app_info);
            }
        }
#endif

        if (!executed && uri) {
            g_app_info_launch_default_for_uri_async(uri, NULL, NULL, NULL, NULL);
        }
    }
    g_object_unref(item);
}

