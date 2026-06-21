#include "window_private.h"
#include <glib/gi18n.h>
#include <string.h>

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

static char *generate_unique_filename(const char *dir, const char *base_name) {
    char *full = g_build_filename(dir, base_name, NULL);
    if (!g_file_test(full, G_FILE_TEST_EXISTS)) {
        return g_strdup(base_name);
    }
    g_free(full);
    
    char *dot = strrchr(base_name, '.');
    char *name = NULL;
    char *ext = NULL;
    if (dot && dot != base_name) {
        name = g_strndup(base_name, dot - base_name);
        ext = g_strdup(dot);
    } else {
        name = g_strdup(base_name);
        ext = g_strdup("");
    }
    
    int i = 1;
    char *new_base = NULL;
    while (1) {
        new_base = g_strdup_printf("%s (%d)%s", name, i, ext);
        full = g_build_filename(dir, new_base, NULL);
        if (!g_file_test(full, G_FILE_TEST_EXISTS)) {
            g_free(full);
            break;
        }
        g_free(full);
        g_free(new_base);
        i++;
    }
    
    g_free(name);
    g_free(ext);
    return new_base;
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
    char *unique_name = generate_unique_filename(self->current_path, "New Folder");
    gtk_editable_set_text(GTK_EDITABLE(entry), unique_name);
    g_free(unique_name);
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
        aether_file_repository_list_directory_async(
            w->repo, w->current_path, NULL, on_directory_loaded, w);
    }
    g_free(full);
    g_object_unref(dir);
    gtk_window_destroy(GTK_WINDOW(d));
}

void on_btn_toggle_sidebar_toggled(GtkToggleButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GtkWidget *sidebar = gtk_paned_get_start_child(GTK_PANED(self->split_view));
    if (sidebar) {
        gboolean active = gtk_toggle_button_get_active(btn);
        gtk_widget_set_visible(sidebar, active);
    }
}

typedef struct {
    AetherWindow *win;
    char *template_path;
} NewDocCtx;

static void on_new_doc_name_response(GtkDialog *d, int response_id, gpointer ud) {
    NewDocCtx *ctx = ud;
    if (response_id != GTK_RESPONSE_ACCEPT) {
        g_free(ctx->template_path);
        g_free(ctx);
        gtk_window_destroy(GTK_WINDOW(d));
        return;
    }
    
    GtkWidget *ent = GTK_WIDGET(g_object_get_data(G_OBJECT(d), "entry"));
    const char *nm = gtk_editable_get_text(GTK_EDITABLE(ent));
    if (!nm || nm[0] == '\0') {
        g_free(ctx->template_path);
        g_free(ctx);
        gtk_window_destroy(GTK_WINDOW(d));
        return;
    }
    
    char *full = g_build_filename(ctx->win->current_path, nm, NULL);
    GError *err = NULL;
    
    if (ctx->template_path) {
        GFile *src = g_file_new_for_path(ctx->template_path);
        GFile *dest = g_file_new_for_path(full);
        if (!g_file_copy(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err)) {
            if (err->code == G_IO_ERROR_PERMISSION_DENIED && aether_privileged_is_available()) {
                g_error_free(err);
                aether_privileged_copy_async(ctx->template_path, full, (GAsyncReadyCallback)on_privileged_action_done, ctx->win);
            } else {
                g_printerr("template copy error: %s\n", err->message);
                g_error_free(err);
            }
        } else {
            aether_file_repository_list_directory_async(
                ctx->win->repo, ctx->win->current_path, NULL, on_directory_loaded, ctx->win);
        }
        g_object_unref(src);
        g_object_unref(dest);
    } else {
        GFile *file = g_file_new_for_path(full);
        GFileOutputStream *stream = g_file_create(file, G_FILE_CREATE_NONE, NULL, &err);
        if (stream) {
            g_object_unref(stream);
            aether_file_repository_list_directory_async(
                ctx->win->repo, ctx->win->current_path, NULL, on_directory_loaded, ctx->win);
        } else if (err) {
            if (err->code == G_IO_ERROR_PERMISSION_DENIED && aether_privileged_is_available()) {
                g_error_free(err);
                aether_privileged_touch_async(full, (GAsyncReadyCallback)on_privileged_action_done, ctx->win);
            } else {
                g_printerr("touch error: %s\n", err->message);
                g_error_free(err);
            }
        }
        g_object_unref(file);
    }
    
    g_free(full);
    g_free(ctx->template_path);
    g_free(ctx);
    gtk_window_destroy(GTK_WINDOW(d));
}

static void on_template_selected(GtkListBox *box, GtkListBoxRow *row, gpointer ud) {
    (void)box;
    GtkDialog *tmpl_dlg = GTK_DIALOG(ud);
    AetherWindow *win = AETHER_WINDOW(g_object_get_data(G_OBJECT(tmpl_dlg), "window"));
    const char *tmpl_path = g_object_get_data(G_OBJECT(row), "tmpl-path");
    
    gtk_window_destroy(GTK_WINDOW(tmpl_dlg));
    
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_OTHER,
                                               GTK_BUTTONS_NONE,
                                               "Enter a name for the new document:");
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dialog), "Create", GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Document name");
    
    if (tmpl_path) {
        char *bn = g_path_get_basename(tmpl_path);
        char *unique_bn = generate_unique_filename(win->current_path, bn);
        gtk_editable_set_text(GTK_EDITABLE(entry), unique_bn);
        g_free(unique_bn);
        g_free(bn);
    } else {
        char *unique_bn = generate_unique_filename(win->current_path, "New Document.txt");
        gtk_editable_set_text(GTK_EDITABLE(entry), unique_bn);
        g_free(unique_bn);
    }
    
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    
    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    gtk_box_append(GTK_BOX(area), entry);
    g_object_set_data(G_OBJECT(dialog), "entry", entry);

    NewDocCtx *ctx = g_new0(NewDocCtx, 1);
    ctx->win = win;
    ctx->template_path = g_strdup(tmpl_path);
    
    g_signal_connect(dialog, "response", G_CALLBACK(on_new_doc_name_response), ctx);
    gtk_window_present(GTK_WINDOW(dialog));
}

void on_new_document_clicked(GtkButton *btn, gpointer user_data) {
    (void)btn;
    AetherWindow *self = AETHER_WINDOW(user_data);
    if (!self->current_path) return;
    
    const char *templates_dir = g_get_user_special_dir(G_USER_DIRECTORY_TEMPLATES);
    char *fallback_dir = NULL;
    if (!templates_dir) {
        fallback_dir = g_build_filename(g_get_home_dir(), "Templates", NULL);
        templates_dir = fallback_dir;
    }
    
    GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(self),
                                               GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                               GTK_MESSAGE_OTHER,
                                               GTK_BUTTONS_CANCEL,
                                               "Choose a Template");
    g_object_set_data(G_OBJECT(dialog), "window", self);
    
    GtkWidget *list_box = gtk_list_box_new();
    gtk_widget_add_css_class(list_box, "transparent-list");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_SINGLE);
    
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *lbl = gtk_label_new("Empty Document");
    gtk_widget_set_margin_top(lbl, 8);
    gtk_widget_set_margin_bottom(lbl, 8);
    gtk_widget_set_margin_start(lbl, 12);
    gtk_widget_set_margin_end(lbl, 12);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
    g_object_set_data(G_OBJECT(row), "tmpl-path", NULL);
    gtk_list_box_append(GTK_LIST_BOX(list_box), row);
    
    if (templates_dir) {
        GDir *dir = g_dir_open(templates_dir, 0, NULL);
        if (dir) {
            const char *name;
            while ((name = g_dir_read_name(dir)) != NULL) {
                if (name[0] == '.') continue; // Skip hidden files
                char *path = g_build_filename(templates_dir, name, NULL);
                if (g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
                    GtkWidget *r = gtk_list_box_row_new();
                    GtkWidget *l = gtk_label_new(name);
                    gtk_widget_set_margin_top(l, 8);
                    gtk_widget_set_margin_bottom(l, 8);
                    gtk_widget_set_margin_start(l, 12);
                    gtk_widget_set_margin_end(l, 12);
                    gtk_label_set_xalign(GTK_LABEL(l), 0.0f);
                    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(r), l);
                    g_object_set_data_full(G_OBJECT(r), "tmpl-path", g_strdup(path), g_free);
                    gtk_list_box_append(GTK_LIST_BOX(list_box), r);
                }
                g_free(path);
            }
            g_dir_close(dir);
        }
    }
    
    g_free(fallback_dir);
    
    g_signal_connect(list_box, "row-activated", G_CALLBACK(on_template_selected), dialog);
    
    GtkWidget *area = gtk_message_dialog_get_message_area(GTK_MESSAGE_DIALOG(dialog));
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_widget_add_css_class(scrolled, "transparent-scrolled");
    gtk_widget_set_size_request(scrolled, 300, 250);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);
    gtk_box_append(GTK_BOX(area), scrolled);
    
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
    
    gtk_window_present(GTK_WINDOW(dialog));
}

void on_open_terminal_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    if (!self->current_path) return;
    /* Try common terminals */
    const char *terms[] = { "vater","kgx", "gnome-terminal", "xterm", "alacritty", NULL };
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

/* ── بيانات حوار التعارض ── */
typedef struct {
    AetherWindow *win;
    char         *dest_dir;
    GPtrArray    *conflict_names; /* أسماء الملفات المتعارضة */
} ConflictData;

static void conflict_data_free(ConflictData *d) {
    g_free(d->dest_dir);
    g_ptr_array_free(d->conflict_names, TRUE);
    g_free(d);
}

/* احتفظ بالنسختين: أضف "(copy)", "(copy 2)", ... حتى لا يوجد تعارض */
static void paste_keep_both(AetherWindow *win, const char *dest_dir) {
    aether_clipboard_paste_keep_both(win->clipboard, dest_dir);
    load_directory(win, win->current_path);
}


static void on_conflict_response(GtkDialog *dlg, int response, gpointer user_data) {
    ConflictData *d = user_data;
    AetherWindow *win = d->win;
    gtk_window_destroy(GTK_WINDOW(dlg));

    if (response == GTK_RESPONSE_ACCEPT) {
        /* Replace: لصق مع استبدال */
        if (win->progress_spinner) gtk_spinner_start(GTK_SPINNER(win->progress_spinner));
        aether_clipboard_paste_with_flags(win->clipboard, d->dest_dir,
                                          G_FILE_COPY_OVERWRITE,
                                          on_paste_done, win);
    } else if (response == GTK_RESPONSE_YES) {
        /* Keep Both: انسخ مع تغيير الاسم */
        paste_keep_both(win, d->dest_dir);
    }
    /* Cancel: لا شيء */

    conflict_data_free(d);
}

static void show_conflict_dialog(AetherWindow *win, const char *dest_dir,
                                  GPtrArray *conflicts) {
    /* بناء قائمة الأسماء */
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

    /* بناء الرسالة */
    const char *op_word = (aether_clipboard_get_op(win->clipboard) == AETHER_CLIPBOARD_COPY)
                          ? "Copying" : "Moving";
    char *msg = g_strdup_printf(
        "%s %u item%s to a location where item%s with the same name already exist%s.\n\n"
        "%s\n\n"
        "What would you like to do?",
        op_word,
        conflicts->len,
        conflicts->len > 1 ? "s" : "",
        conflicts->len > 1 ? "s" : "",
        conflicts->len > 1 ? "" : "s",
        names_str->str);
    g_string_free(names_str, TRUE);

    GtkWidget *dlg = gtk_message_dialog_new(
        GTK_WINDOW(win),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE,
        "File Conflict");
    gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", msg);
    g_free(msg);

    gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancel",       GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Keep Both",    GTK_RESPONSE_YES);
    GtkWidget *replace_btn = gtk_dialog_add_button(GTK_DIALOG(dlg), "Replace", GTK_RESPONSE_ACCEPT);
    gtk_widget_add_css_class(replace_btn, "destructive-action");
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

    ConflictData *d = g_new0(ConflictData, 1);
    d->win            = win;
    d->dest_dir       = g_strdup(dest_dir);
    d->conflict_names = conflicts; /* يأخذ ملكية المصفوفة */

    g_signal_connect(dlg, "response", G_CALLBACK(on_conflict_response), d);
    gtk_window_present(GTK_WINDOW(dlg));
}

void on_paste_toolbar_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    (void)btn;
    if (!self->current_path) return;
    if (!aether_clipboard_has_content(self->clipboard)) return;

    /* فحص التعارضات أولاً */
    GPtrArray *conflicts = aether_clipboard_find_conflicts(
                               self->clipboard, self->current_path);
    if (conflicts->len > 0) {
        /* أظهر حوار التعارض — يتولى الحوار تنفيذ العملية بعد الاختيار */
        show_conflict_dialog(self, self->current_path, conflicts);
    } else {
        g_ptr_array_free(conflicts, TRUE);
        if (self->progress_spinner) gtk_spinner_start(GTK_SPINNER(self->progress_spinner));
        aether_clipboard_paste(self->clipboard, self->current_path,
            on_paste_done, self);
    }
}

void on_paste_done(GObject *src, GAsyncResult *res, gpointer ud) {
    (void)src;
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
    
    GPtrArray *conflicts = aether_clipboard_find_conflicts(w->clipboard, w->current_path);
    if (conflicts->len > 0) {
        show_conflict_dialog(w, w->current_path, conflicts);
    } else {
        g_ptr_array_free(conflicts, TRUE);
        if (w->progress_spinner) gtk_spinner_start(GTK_SPINNER(w->progress_spinner));
        aether_clipboard_paste(w->clipboard, w->current_path, on_paste_done, w);
    }
    
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
    if (!aether_file_entity_is_directory(entity)) return FALSE;

    /* رفض الإسقاط إذا كان المجلد الوجهة ضمن العناصر المسحوبة */
    const char *dest_path = aether_file_entity_get_path(entity);
    if (!dest_path) return FALSE;

    GtkWidget *child = gtk_list_item_get_child(list_item);
    GtkWidget *win_widget = child ? gtk_widget_get_ancestor(child, AETHER_TYPE_WINDOW) : NULL;
    if (win_widget) {
        AetherWindow *win = AETHER_WINDOW(win_widget);
        GStrv sel = aether_window_get_selected_paths(win);
        if (sel) {
            for (int i = 0; sel[i] != NULL; i++) {
                if (g_strcmp0(sel[i], dest_path) == 0) {
                    g_strfreev(sel);
                    return FALSE; /* الوجهة محددة — ارفض الإسقاط */
                }
            }
            g_strfreev(sel);
        }
    }

    return TRUE;
}

typedef struct {
    AetherWindow *win;
    char         *dest_dir;
    GPtrArray    *src_paths;      /* GPtrArray of char* (takes ownership) */
    GPtrArray    *conflict_names; /* GPtrArray of char* (takes ownership) */
} DnDConflictData;

static void dnd_conflict_data_free(DnDConflictData *d) {
    g_free(d->dest_dir);
    g_ptr_array_free(d->src_paths, TRUE);
    g_ptr_array_free(d->conflict_names, TRUE);
    g_free(d);
}

/* بيانات مساعدة لإعادة تحميل النافذة بعد انتهاء نقل DnD عبر الـ daemon */
typedef struct {
    AetherWindow *win;
    int           pending;
} DnDPrivCtx;

static void on_dnd_priv_done(GObject *src, GAsyncResult *res, gpointer ud) {
    (void)src;
    DnDPrivCtx *ctx = ud;
    GError *err = NULL;
    if (!aether_privileged_op_finish(res, &err)) {
        g_printerr("DnD privileged move failed: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
    }
    ctx->pending--;
    if (ctx->pending <= 0) {
        aether_window_reload(ctx->win);
        g_free(ctx);
    }
}

static void execute_dnd_move(AetherWindow *win, const char *dest_dir, GPtrArray *src_paths, GFileCopyFlags flags) {
    GFile *dest_gdir = g_file_new_for_path(dest_dir);

    /* نتتبع الملفات التي تحتاج privileged */
    GPtrArray *priv_srcs  = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *priv_dests = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < src_paths->len; i++) {
        const char *src_path = g_ptr_array_index(src_paths, i);
        if (!src_path) continue;

        GFile *src = g_file_new_for_path(src_path);
        char *basename = g_file_get_basename(src);
        GFile *dest_file = g_file_get_child(dest_gdir, basename);
        char *dest_path  = g_file_get_path(dest_file);
        GError *err = NULL;

        g_file_move(src, dest_file, flags, NULL, NULL, NULL, &err);
        if (err) {
            if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
                aether_privileged_is_available()) {
                /* أضف للقائمة المحمية */
                g_ptr_array_add(priv_srcs,  g_strdup(src_path));
                g_ptr_array_add(priv_dests, g_strdup(dest_path));
            } else {
                g_printerr("DnD move error: %s\n", err->message);
            }
            g_error_free(err);
        }

        g_free(dest_path);
        g_object_unref(dest_file);
        g_free(basename);
        g_object_unref(src);
    }
    g_object_unref(dest_gdir);

    if (priv_srcs->len > 0) {
        /* شغّل الـ daemon إذا لم يكن يعمل */
        if (!aether_privileged_daemon_is_running())
            aether_privileged_daemon_start();

        DnDPrivCtx *ctx = g_new0(DnDPrivCtx, 1);
        ctx->win     = win;
        ctx->pending = (int)priv_srcs->len;
        for (guint i = 0; i < priv_srcs->len; i++) {
            aether_privileged_move_async(
                g_ptr_array_index(priv_srcs, i),
                g_ptr_array_index(priv_dests, i),
                on_dnd_priv_done, ctx);
        }
    } else {
        aether_window_reload(win);
    }

    g_ptr_array_free(priv_srcs,  TRUE);
    g_ptr_array_free(priv_dests, TRUE);
}

static void execute_dnd_move_keep_both(AetherWindow *win, const char *dest_dir, GPtrArray *src_paths) {
    GFile *dest_gdir = g_file_new_for_path(dest_dir);

    GPtrArray *priv_srcs  = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *priv_dests = g_ptr_array_new_with_free_func(g_free);

    for (guint i = 0; i < src_paths->len; i++) {
        const char *src_path = g_ptr_array_index(src_paths, i);
        if (!src_path) continue;

        GFile *src = g_file_new_for_path(src_path);
        char *basename = g_file_get_basename(src);

        GFile *dest_check = g_file_new_for_path(g_build_filename(dest_dir, basename, NULL));
        gboolean conflict = g_file_query_exists(dest_check, NULL);
        g_object_unref(dest_check);

        char *dest_file_path = NULL;
        if (conflict) {
            char *dot = strrchr(basename, '.');
            char *name_part = dot ? g_strndup(basename, dot - basename) : g_strdup(basename);
            const char *ext = dot ? dot : "";

            int counter = 0;
            while (TRUE) {
                char *candidate;
                if (counter == 0)
                    candidate = g_strdup_printf("%s (copy)%s", name_part, ext);
                else
                    candidate = g_strdup_printf("%s (copy %d)%s", name_part, counter + 1, ext);

                dest_file_path = g_build_filename(dest_dir, candidate, NULL);
                g_free(candidate);

                GFile *test = g_file_new_for_path(dest_file_path);
                gboolean exists = g_file_query_exists(test, NULL);
                g_object_unref(test);

                if (!exists) break;
                g_free(dest_file_path);
                dest_file_path = NULL;
                counter++;
            }
            g_free(name_part);
        } else {
            dest_file_path = g_build_filename(dest_dir, basename, NULL);
        }

        GFile *dest_file = g_file_new_for_path(dest_file_path);
        GError *err = NULL;
        g_file_move(src, dest_file, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
        if (err) {
            if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
                aether_privileged_is_available()) {
                g_ptr_array_add(priv_srcs,  g_strdup(src_path));
                g_ptr_array_add(priv_dests, g_strdup(dest_file_path));
            } else {
                g_printerr("DnD keep-both error: %s\n", err->message);
            }
            g_error_free(err);
        }

        g_object_unref(dest_file);
        g_free(dest_file_path);
        g_free(basename);
        g_object_unref(src);
    }
    g_object_unref(dest_gdir);

    if (priv_srcs->len > 0) {
        if (!aether_privileged_daemon_is_running())
            aether_privileged_daemon_start();

        DnDPrivCtx *ctx = g_new0(DnDPrivCtx, 1);
        ctx->win     = win;
        ctx->pending = (int)priv_srcs->len;
        for (guint i = 0; i < priv_srcs->len; i++) {
            aether_privileged_move_async(
                g_ptr_array_index(priv_srcs, i),
                g_ptr_array_index(priv_dests, i),
                on_dnd_priv_done, ctx);
        }
    } else {
        aether_window_reload(win);
    }

    g_ptr_array_free(priv_srcs,  TRUE);
    g_ptr_array_free(priv_dests, TRUE);
}

static void on_dnd_conflict_response(GtkDialog *dlg, int response, gpointer user_data) {
    DnDConflictData *d = user_data;
    AetherWindow *win = d->win;
    gtk_window_destroy(GTK_WINDOW(dlg));

    if (response == GTK_RESPONSE_ACCEPT) {
        /* Replace: move with G_FILE_COPY_OVERWRITE */
        execute_dnd_move(win, d->dest_dir, d->src_paths, G_FILE_COPY_OVERWRITE);
    } else if (response == GTK_RESPONSE_YES) {
        /* Keep Both: move and rename if conflict */
        execute_dnd_move_keep_both(win, d->dest_dir, d->src_paths);
    }
    /* Cancel: do nothing */

    dnd_conflict_data_free(d);
}

void aether_window_handle_dnd_move(AetherWindow *win, const char *dest_path, GPtrArray *src_paths) {
    if (!dest_path || !src_paths || src_paths->len == 0) return;

    /* فحص التعارضات أولاً */
    GPtrArray *conflicts = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < src_paths->len; i++) {
        const char *src_path = g_ptr_array_index(src_paths, i);
        if (!src_path) continue;

        GFile *src = g_file_new_for_path(src_path);
        char *basename = g_file_get_basename(src);
        char *dest_file_path = g_build_filename(dest_path, basename, NULL);
        GFile *dest_file = g_file_new_for_path(dest_file_path);

        if (g_file_query_exists(dest_file, NULL)) {
            g_ptr_array_add(conflicts, g_strdup(basename));
        }

        g_object_unref(dest_file);
        g_free(dest_file_path);
        g_free(basename);
        g_object_unref(src);
    }

    if (conflicts->len > 0) {
        /* أظهر حوار التعارض */
        GString *names_str = g_string_new("");
        guint show = MIN(conflicts->len, 5);
        for (guint i = 0; i < show; i++) {
            if (i > 0) g_string_append(names_str, "\n");
            g_string_append_printf(names_str, "• %s", (char *)g_ptr_array_index(conflicts, i));
        }
        if (conflicts->len > 5) {
            g_string_append_printf(names_str, "\n… and %u more", conflicts->len - 5);
        }

        char *msg = g_strdup_printf(
            "Moving %u item%s to a location where item%s with the same name already exist%s.\n\n"
            "%s\n\nWhat would you like to do?",
            conflicts->len,
            conflicts->len > 1 ? "s" : "",
            conflicts->len > 1 ? "s" : "",
            conflicts->len > 1 ? "" : "s",
            names_str->str);
        g_string_free(names_str, TRUE);

        GtkWidget *dlg = gtk_message_dialog_new(
            GTK_WINDOW(win),
            GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_QUESTION,
            GTK_BUTTONS_NONE,
            "File Conflict");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dlg), "%s", msg);
        g_free(msg);

        gtk_dialog_add_button(GTK_DIALOG(dlg), "Cancel",    GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(dlg), "Keep Both", GTK_RESPONSE_YES);
        GtkWidget *rb = gtk_dialog_add_button(GTK_DIALOG(dlg), "Replace", GTK_RESPONSE_ACCEPT);
        gtk_widget_add_css_class(rb, "destructive-action");
        gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

        DnDConflictData *d = g_new0(DnDConflictData, 1);
        d->win = win;
        d->dest_dir = g_strdup(dest_path);
        d->src_paths = src_paths; /* takes ownership */
        d->conflict_names = conflicts; /* takes ownership */

        g_signal_connect(dlg, "response", G_CALLBACK(on_dnd_conflict_response), d);
        gtk_window_present(GTK_WINDOW(dlg));
    } else {
        g_ptr_array_free(conflicts, TRUE);
        /* تنفيذ النقل مباشرة */
        execute_dnd_move(win, dest_path, src_paths, G_FILE_COPY_NONE);
        g_ptr_array_free(src_paths, TRUE);
    }
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

    GtkWidget *box = gtk_list_item_get_child(list_item);
    GtkWidget *root = box ? GTK_WIDGET(gtk_widget_get_root(box)) : NULL;
    if (!root || !AETHER_IS_WINDOW(root)) return FALSE;
    AetherWindow *win = AETHER_WINDOW(root);

    GdkFileList *file_list = g_value_get_boxed(value);
    if (!file_list) return FALSE;

    GSList *files = gdk_file_list_get_files(file_list);
    GPtrArray *src_paths = g_ptr_array_new_with_free_func(g_free);

    for (GSList *l = files; l != NULL; l = l->next) {
        GFile *src = G_FILE(l->data);
        char *src_path = g_file_get_path(src);

        /* تخطّ الملف إذا كان مساره هو نفس مسار الوجهة (سحب مجلد إلى نفسه) */
        if (src_path && g_strcmp0(src_path, dest_path) == 0) {
            g_free(src_path);
            continue;
        }

        /* تخطّ الملف إذا كانت الوجهة داخل الملف المصدر (نقل مجلد إلى داخل نفسه) */
        if (src_path) {
            char *src_with_slash = g_strdup_printf("%s/", src_path);
            if (g_str_has_prefix(dest_path, src_with_slash)) {
                g_free(src_with_slash);
                g_free(src_path);
                continue;
            }
            g_free(src_with_slash);
        }

        if (src_path) {
            g_ptr_array_add(src_paths, src_path);
        }
    }

    if (src_paths->len > 0) {
        aether_window_handle_dnd_move(win, dest_path, src_paths);
    } else {
        g_ptr_array_free(src_paths, TRUE);
    }

    return TRUE;
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

