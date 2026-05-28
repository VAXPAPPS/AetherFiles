#include "window_private.h"
#include <glib/gi18n.h>

/* ── دالة مشتركة لإعادة التحميل بعد العملية المحمية ── */
static void on_privileged_action_done(GObject *src, GAsyncResult *res, gpointer ud) {
    (void)src;
    AetherWindow *w = AETHER_WINDOW(ud);
    aether_window_stop_progress(w);
    GError *err = NULL;
    if (!aether_privileged_op_finish(res, &err)) {
        g_printerr("Privileged action failed: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
    }
    load_directory(w, w->current_path);
}

void on_new_folder_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AetherWindow *self = AETHER_WINDOW(user_data);
    if (!self->current_path) return;

    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(self),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_OTHER,
                                               GTK_BUTTONS_NONE,
                                               "Enter a name for the new folder:");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Create", GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Folder name");
    gtk_editable_set_text(GTK_EDITABLE(entry), "New Folder");
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    
    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gtk_box_append(GTK_BOX(area), entry);

    /* Store self + entry for the response handler */
    g_object_set_data(G_OBJECT(dialog), "window", self);
    g_object_set_data(G_OBJECT(dialog), "entry",  entry);

    g_signal_connect(dialog, "response",
        G_CALLBACK(on_new_folder_response), NULL);

    gtk_window_present(GTK_WINDOW(dialog));
}

void on_new_folder_response(GtkDialog *d, int response_id, gpointer ud) {
    (void)ud;
    if (response_id != GTK_RESPONSE_ACCEPT) {
        gtk_window_destroy(GTK_WINDOW(d));
        return;
    }
    AetherWindow *w   = AETHER_WINDOW(g_object_get_data(G_OBJECT(d), "window"));
    GtkWidget    *ent = GTK_WIDGET(g_object_get_data(G_OBJECT(d), "entry"));
    const char   *nm  = gtk_editable_get_text(GTK_EDITABLE(ent));
    if (!nm || nm[0] == '\0') {
        gtk_window_destroy(GTK_WINDOW(d));
        return;
    }
    char  *full = g_build_filename(w->current_path, nm, NULL);
    GFile *dir  = g_file_new_for_path(full);
    GError *err = NULL;
    if (!g_file_make_directory(dir, NULL, &err)) {
        /* عند فشل إنشاء المجلد بسبب الصلاحيات، حاول عبر المساعد */
        if (err && err->code == G_IO_ERROR_PERMISSION_DENIED &&
            aether_privileged_is_available()) {
            g_error_free(err);
            aether_privileged_mkdir_async(full,
                (GAsyncReadyCallback)on_privileged_action_done, w);
        } else {
            g_printerr("mkdir error: %s\n", err ? err->message : "?");
            if (err) g_error_free(err);
        }
    } else {
        load_directory(w, w->current_path);
    }
    g_free(full);
    g_object_unref(dir);
    gtk_window_destroy(GTK_WINDOW(d));
}

void on_new_document_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    if (!self->current_path) return;
    char  *full = g_build_filename(self->current_path, "New Document.txt", NULL);
    GFile *file = g_file_new_for_path(full);
    GError *err = NULL;
    GFileOutputStream *stream = g_file_create(file, G_FILE_CREATE_NONE, NULL, &err);
    if (stream) {
        g_object_unref(stream);
        load_directory(self, self->current_path);
    } else if (err) {
        if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
            aether_privileged_is_available()) {
            g_error_free(err);
            aether_privileged_touch_async(full,
                (GAsyncReadyCallback)on_privileged_action_done, self);
        } else {
            g_printerr("touch error: %s\n", err->message);
            g_error_free(err);
        }
    }
    g_free(full);
    g_object_unref(file);
}

void on_open_terminal_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    if (!self->current_path) return;
    /* Try common terminals */
    const char *terms[] = { "kgx", "gnome-terminal", "xterm", "alacritty", NULL };
    for (int i = 0; terms[i]; i++) {
        char *cmd = g_strdup_printf("%s --working-directory=\"%s\" &",
                                    terms[i], self->current_path);
        if (g_spawn_command_line_async(cmd, NULL)) {
            g_free(cmd);
            return;
        }
        g_free(cmd);
    }
}

void on_select_all_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(self->view_stack));
    if (g_strcmp0(visible, "grid") == 0) {
        GtkSelectionModel *sel = gtk_grid_view_get_model(GTK_GRID_VIEW(self->grid_view));
        if (sel) gtk_selection_model_select_all(sel);
    } else {
        GtkSelectionModel *sel = gtk_column_view_get_model(GTK_COLUMN_VIEW(self->list_view));
        if (sel) gtk_selection_model_select_all(sel);
    }
}

void on_paste_toolbar_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    (void)btn;
    if (!self->current_path) return;
    if (!aether_clipboard_has_content(self->clipboard)) return;
    
    if (self->progress_spinner) gtk_spinner_start(GTK_SPINNER(self->progress_spinner));
    aether_clipboard_paste(self->clipboard, self->current_path,
        on_paste_done, self);
}

void on_paste_done(GObject *src, GAsyncResult *res, gpointer ud) {
    AetherWindow *w = AETHER_WINDOW(ud);
    if (w->progress_spinner) gtk_spinner_stop(GTK_SPINNER(w->progress_spinner));
    
    GError *err = NULL;
    aether_clipboard_paste_finish(w->clipboard, res, &err);
    if (err) { g_printerr("Paste error: %s\n", err->message); g_error_free(err); }
    else load_directory(w, w->current_path);
}

void on_sort_mode_changed(GtkDropDown *dropdown, GParamSpec *pspec,
                                  gpointer user_data)
{
    (void)pspec;
    AetherWindow *self = AETHER_WINDOW(user_data);
    self->sort_mode = (int)gtk_drop_down_get_selected(dropdown);
    
    if (self->sort_btn) {
        GtkWidget *popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(self->sort_btn));
        if (popover) gtk_popover_popdown(GTK_POPOVER(popover));
    }
    
    if (self->sorter) {
        gtk_sorter_changed(GTK_SORTER(self->sorter), GTK_SORTER_CHANGE_DIFFERENT);
    }
}

void on_sort_dir_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AetherWindow *self = AETHER_WINDOW(user_data);
    self->sort_asc = !self->sort_asc;
    if (self->sort_btn) {
        gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(self->sort_btn),
            self->sort_asc ? "view-sort-ascending-symbolic"
                           : "view-sort-descending-symbolic");
        GtkWidget *popover = gtk_menu_button_get_popover(GTK_MENU_BUTTON(self->sort_btn));
        if (popover) gtk_popover_popdown(GTK_POPOVER(popover));
    }
    
    if (self->sorter) {
        gtk_sorter_changed(GTK_SORTER(self->sorter), GTK_SORTER_CHANGE_DIFFERENT);
    }
}

void on_toggle_hidden(GSimpleAction *action, GVariant *param, gpointer user_data) {
    (void)action; (void)param;
    AetherWindow *self = AETHER_WINDOW(user_data);
    self->show_hidden = !self->show_hidden;
    
    if (self->btn_hidden) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(self->btn_hidden), self->show_hidden);
    }
    
    if (self->name_filter) {
        gtk_filter_changed(GTK_FILTER(self->name_filter), GTK_FILTER_CHANGE_DIFFERENT);
    }
    update_statusbar(self);
}

gboolean on_key_back(GtkWidget *w, GVariant *args, gpointer ud) {
    (void)w; (void)args; navigate_back(AETHER_WINDOW(ud)); return TRUE;
}

gboolean on_key_fwd(GtkWidget *w, GVariant *args, gpointer ud) {
    (void)w; (void)args; navigate_forward(AETHER_WINDOW(ud)); return TRUE;
}

gboolean on_key_up(GtkWidget *w, GVariant *args, gpointer ud) {
    (void)w; (void)args;
    AetherWindow *self = AETHER_WINDOW(ud);
    if (!self->current_path) return TRUE;
    char *parent = g_path_get_dirname(self->current_path);
    if (g_strcmp0(parent, self->current_path) != 0)
        load_directory(self, parent);
    g_free(parent);
    return TRUE;
}

gboolean on_key_refresh(GtkWidget *w, GVariant *args, gpointer ud) {
    (void)w; (void)args;
    AetherWindow *self = AETHER_WINDOW(ud);
    if (self->current_path)
        aether_file_repository_list_directory_async(
            self->repo, self->current_path, NULL, on_directory_loaded, self);
    return TRUE;
}

void on_add_bookmark_action(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p;
    AetherWindow *w = AETHER_WINDOW(ud);
    if (!w->current_path) return;
    save_bookmark(w->current_path);
    load_bookmarks(w);
}

void on_add_bookmark_path_action(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a;
    AetherWindow *w = AETHER_WINDOW(ud);
    const char *path = g_variant_get_string(p, NULL);
    if (!path) return;
    save_bookmark(path);
    load_bookmarks(w);
}

void on_remove_bookmark_action(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a; (void)p;
    AetherWindow *w = AETHER_WINDOW(ud);
    if (!w->current_path) return;
    remove_bookmark(w->current_path);
    load_bookmarks(w);
}

void on_remove_bookmark_path_action(GSimpleAction *a, GVariant *p, gpointer ud) {
    (void)a;
    AetherWindow *w = AETHER_WINDOW(ud);
    const char *path = g_variant_get_string(p, NULL);
    if (!path) return;
    remove_bookmark(path);
    load_bookmarks(w);
}

gboolean on_drop_target(GtkDropTarget *t, const GValue *val,
                               double x, double y, gpointer ud)
{
    (void)t; (void)x; (void)y;
    AetherWindow *w = AETHER_WINDOW(ud);
    if (!G_VALUE_HOLDS(val, GDK_TYPE_FILE_LIST)) return FALSE;
    
    GdkFileList *file_list = g_value_get_boxed(val);
    if (!file_list || !w->current_path) return FALSE;
    
    GdkDrop *drop = gtk_drop_target_get_current_drop(t);
    GdkDragAction action = drop ? gdk_drop_get_actions(drop) : GDK_ACTION_MOVE;
    AetherClipboardOp op = (action & GDK_ACTION_COPY) ? AETHER_CLIPBOARD_COPY : AETHER_CLIPBOARD_CUT;
    
    GSList *files = gdk_file_list_get_files(file_list);
    guint n_files = g_slist_length(files);
    GStrv paths = g_new0(char *, n_files + 1);
    
    guint i = 0;
    for (GSList *l = files; l != NULL; l = l->next) {
        GFile *src_file = G_FILE(l->data);
        paths[i++] = g_file_get_path(src_file);
    }
    
    aether_clipboard_set(w->clipboard, paths, op);
    g_strfreev(paths);
    
    if (w->progress_spinner) gtk_spinner_start(GTK_SPINNER(w->progress_spinner));
    aether_clipboard_paste(w->clipboard, w->current_path, on_paste_done, w);
    
    return TRUE;
}

void setup_drag_drop(AetherWindow *self, GtkWidget *view) {
    GtkDropTarget *target = gtk_drop_target_new(GDK_TYPE_FILE_LIST,
                                GDK_ACTION_MOVE | GDK_ACTION_COPY);
    g_signal_connect(target, "drop", G_CALLBACK(on_drop_target), self);
    gtk_widget_add_controller(view, GTK_EVENT_CONTROLLER(target));
}

GdkContentProvider *on_drag_prepare(GtkDragSource *source, double x, double y,
                             gpointer user_data)
{
    (void)source; (void)x; (void)y;
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item || !AETHER_IS_FILE_ENTITY(item)) return NULL;
    
    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    const char *dragged_path = aether_file_entity_get_path(entity);
    if (!dragged_path) return NULL;
    
    GtkWidget *child = gtk_list_item_get_child(list_item);
    GtkWidget *win_widget = child ? gtk_widget_get_ancestor(child, AETHER_TYPE_WINDOW) : NULL;
    
    GSList *files = NULL;
    gboolean used_selection = FALSE;
    
    if (win_widget) {
        AetherWindow *win = AETHER_WINDOW(win_widget);
        GStrv paths = aether_window_get_selected_paths(win);
        if (paths) {
            gboolean dragged_is_selected = FALSE;
            for (int i = 0; paths[i] != NULL; i++) {
                if (g_strcmp0(paths[i], dragged_path) == 0) {
                    dragged_is_selected = TRUE;
                    break;
                }
            }
            
            if (dragged_is_selected) {
                used_selection = TRUE;
                for (int i = 0; paths[i] != NULL; i++) {
                    files = g_slist_prepend(files, g_file_new_for_path(paths[i]));
                }
                files = g_slist_reverse(files);
            }
            g_strfreev(paths);
        }
    }
    
    if (!used_selection) {
        files = g_slist_append(files, g_file_new_for_path(dragged_path));
    }
    
    GdkFileList *file_list = gdk_file_list_new_from_list(files);
    GValue value = G_VALUE_INIT;
    g_value_init(&value, GDK_TYPE_FILE_LIST);
    g_value_take_boxed(&value, file_list);
    
    GdkContentProvider *provider = gdk_content_provider_new_for_value(&value);
    g_value_unset(&value);
    g_slist_free_full(files, g_object_unref);
    return provider;
    
    return provider;
}

gboolean on_item_drop_accept(GtkDropTarget *target, GdkDrop *drop, gpointer user_data) {
    (void)target; (void)drop;
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item || !AETHER_IS_FILE_ENTITY(item)) return FALSE;
    
    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    return aether_file_entity_is_directory(entity);
}

gboolean on_item_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data) {
    (void)target; (void)x; (void)y;
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item || !AETHER_IS_FILE_ENTITY(item)) return FALSE;
    
    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    if (!aether_file_entity_is_directory(entity)) return FALSE;
    
    const char *dest_path = aether_file_entity_get_path(entity);
    if (!dest_path) return FALSE;
    
    GdkFileList *file_list = g_value_get_boxed(value);
    if (!file_list) return FALSE;
    
    GSList *files = gdk_file_list_get_files(file_list);
    GFile *dest_dir = g_file_new_for_path(dest_path);
    gboolean success = TRUE;
    
    for (GSList *l = files; l != NULL; l = l->next) {
        GFile *src = G_FILE(l->data);
        char *basename = g_file_get_basename(src);
        GFile *dest_file = g_file_get_child(dest_dir, basename);
        GError *err = NULL;
        
        g_file_move(src, dest_file, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
        if (err) {
            g_printerr("DnD error: Error moving file %s: %s\n", dest_path, err->message);
            g_error_free(err);
            success = FALSE;
        }
        g_object_unref(dest_file);
        g_free(basename);
    }
    g_object_unref(dest_dir);
    g_slist_free(files);
    
    GtkWidget *box = gtk_list_item_get_child(list_item);
    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(box));
    if (root && AETHER_IS_WINDOW(root)) {
        aether_window_reload(AETHER_WINDOW(root));
    }
    
    return success;
}

gboolean on_undo_shortcut(GtkWidget *w, GVariant *a, gpointer ud) {
    (void)w; (void)a;
    do_undo(AETHER_WINDOW(ud));
    return TRUE;
}

gboolean on_ctrl_h_shortcut(GtkWidget *w, GVariant *a, gpointer ud) {
    (void)w; (void)a;
    AetherWindow *self = AETHER_WINDOW(ud);
    self->show_hidden = !self->show_hidden;
    if (self->current_path)
        aether_file_repository_list_directory_async(
            self->repo, self->current_path, NULL, on_directory_loaded, self);
    return TRUE;
}

gboolean on_ctrl_d_shortcut(GtkWidget *w, GVariant *a, gpointer ud) {
    (void)w; (void)a;
    AetherWindow *self = AETHER_WINDOW(ud);
    if (!self->current_path) return TRUE;
    save_bookmark(self->current_path);
    load_bookmarks(self);
    return TRUE;
}

void push_undo(AetherWindow *self, UndoOp op, const char *src, const char *dest) {
    UndoEntry *e = g_new0(UndoEntry, 1);
    e->op   = op;
    e->src  = g_strdup(src);
    e->dest = g_strdup(dest);
    g_ptr_array_add(self->undo_stack, e);
    g_ptr_array_set_size(self->redo_stack, 0); /* clear redo on new action */
}

void do_undo(AetherWindow *self) {
    if (!self->undo_stack || self->undo_stack->len == 0) return;
    UndoEntry *e = g_ptr_array_steal_index(self->undo_stack,
                                            self->undo_stack->len - 1);
    GError *err = NULL;
    switch (e->op) {
    case UNDO_TRASH: {
        /* Restore from trash — locate in trash:// */
        GFile *trash_file = NULL;
        GFileEnumerator *en = g_file_enumerate_children(
            g_file_new_for_uri("trash:///"),
            G_FILE_ATTRIBUTE_STANDARD_NAME ","
            G_FILE_ATTRIBUTE_TRASH_ORIG_PATH,
            G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (en) {
            GFileInfo *info;
            while ((info = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
                const char *orig = g_file_info_get_attribute_byte_string(
                                      info, G_FILE_ATTRIBUTE_TRASH_ORIG_PATH);
                if (orig && g_strcmp0(orig, e->src) == 0) {
                    const char *name = g_file_info_get_name(info);
                    trash_file = g_file_get_child(g_file_new_for_uri("trash:///"), name);
                    g_object_unref(info);
                    break;
                }
                g_object_unref(info);
            }
            g_object_unref(en);
        }
        if (trash_file) {
            GFile *dest = g_file_new_for_path(e->src);
            g_file_move(trash_file, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
            if (err) { g_printerr("Undo trash error: %s\n", err->message); g_error_free(err); }
            g_object_unref(trash_file);
            g_object_unref(dest);
        }
        break;
    }
    case UNDO_RENAME: {
        GFile *curr = g_file_new_for_path(e->dest); /* renamed-to path */
        GFile *restored = g_file_set_display_name(curr, g_path_get_basename(e->src), NULL, &err);
        if (err) { g_printerr("Undo rename error: %s\n", err->message); g_error_free(err); }
        if (restored) g_object_unref(restored);
        g_object_unref(curr);
        break;
    }
    case UNDO_MOVE: {
        GFile *src = g_file_new_for_path(e->dest);
        GFile *dst = g_file_new_for_path(e->src);
        g_file_move(src, dst, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
        if (err) { g_printerr("Undo move error: %s\n", err->message); g_error_free(err); }
        g_object_unref(src); g_object_unref(dst);
        break;
    }
    }
    /* Push to redo */
    g_ptr_array_add(self->redo_stack, e);
    aether_window_reload(self);
}

void on_btn_back_clicked(GtkButton *btn, gpointer user_data) {
    navigate_back(AETHER_WINDOW(user_data));
}

void on_btn_fwd_clicked(GtkButton *btn, gpointer user_data) {
    navigate_forward(AETHER_WINDOW(user_data));
}

void on_view_switched(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *name = g_object_get_data(G_OBJECT(btn), "view-name");
    gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), name);
    gtk_widget_remove_css_class(self->btn_grid, "active-view");
    gtk_widget_remove_css_class(self->btn_list, "active-view");
    gtk_widget_add_css_class(GTK_WIDGET(btn), "active-view");
}

void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    g_free(self->filter_string);
    self->filter_string = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
    if (self->name_filter)
        gtk_filter_changed(GTK_FILTER(self->name_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

void on_search_filter_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data) {
    (void)pspec;
    AetherWindow *self = AETHER_WINDOW(user_data);
    self->search_filter_type = gtk_drop_down_get_selected(dropdown);
    if (self->name_filter)
        gtk_filter_changed(GTK_FILTER(self->name_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

void on_search_mode_toggled(GObject *object, GParamSpec *pspec, gpointer user_data) {
    (void)object;
    (void)pspec;
    AetherWindow *self = AETHER_WINDOW(user_data);
    if (self->name_filter)
        gtk_filter_changed(GTK_FILTER(self->name_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

