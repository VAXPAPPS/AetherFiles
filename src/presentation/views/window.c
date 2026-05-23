#include "window.h"
#include "../../data/gio_file_repository.h"
#include "../controllers/clipboard_controller.h"
#include <gio/gio.h>
#include <string.h>

#define HISTORY_MAX 50

struct _AetherWindow {
    AdwApplicationWindow    parent_instance;
    AetherFileRepository   *repo;
    AetherClipboardController *clipboard;

    char    *current_path;
    gboolean show_hidden;

    /* Navigation history */
    GPtrArray *back_stack;
    GPtrArray *fwd_stack;

    /* Counts */
    guint item_count;
    guint selected_count;

    /* Widgets — views */
    GtkWidget *grid_view;
    GtkWidget *list_view;
    GtkWidget *view_stack;

    /* Widgets — layout */
    GtkWidget *path_bar;
    GtkWidget *sidebar_list;
    GtkWidget *split_view;
    GtkWidget *search_bar;
    GtkWidget *search_entry;
    GtkWidget *status_label;
    GtkWidget *space_label;
    GtkWidget *btn_back;
    GtkWidget *btn_fwd;
    GtkWidget *btn_grid;
    GtkWidget *btn_list;

    /* Filter model for search */
    GtkFilterListModel *filter_model;
    GtkCustomFilter    *name_filter;
    char               *filter_string;
};

G_DEFINE_TYPE(AetherWindow, aether_window, ADW_TYPE_APPLICATION_WINDOW)

static void load_directory(AetherWindow *self, const char *path);
static void update_nav_buttons(AetherWindow *self);
static void update_statusbar(AetherWindow *self);

/* ── CSS loading ── */
static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/aetheros/files/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}

/* ── Popover cleanup ── */
static void on_popover_closed(GtkPopover *popover, gpointer user_data) {
    gtk_widget_unparent(GTK_WIDGET(popover));
}

/* ── Right-click context menu ── */
static void on_item_right_clicked(GtkGestureClick *gesture, int n_press,
                                   double x, double y, gpointer user_data)
{
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item) return;

    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    GtkWidget *box = gtk_list_item_get_child(list_item);
    const char *path = aether_file_entity_get_path(entity);
    GVariant   *pv   = g_variant_new_string(path);

    GMenu *menu = g_menu_new();

    /* Open */
    GMenu *s1 = g_menu_new();
    GMenuItem *mi = g_menu_item_new("Open", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.open", pv);
    g_menu_append_item(s1, mi); g_object_unref(mi);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s1)); g_object_unref(s1);

    /* Clipboard */
    GMenu *s2 = g_menu_new();
    mi = g_menu_item_new("Cut",  NULL);
    g_menu_item_set_action_and_target_value(mi, "app.cut",  pv);
    g_menu_append_item(s2, mi); g_object_unref(mi);
    mi = g_menu_item_new("Copy", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.copy", pv);
    g_menu_append_item(s2, mi); g_object_unref(mi);
    mi = g_menu_item_new("Paste", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.paste", pv);
    g_menu_append_item(s2, mi); g_object_unref(mi);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s2)); g_object_unref(s2);

    /* Manage */
    GMenu *s3 = g_menu_new();
    mi = g_menu_item_new("Rename…", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.rename", pv);
    g_menu_append_item(s3, mi); g_object_unref(mi);

    if (!aether_file_entity_is_directory(entity)) {
        mi = g_menu_item_new("Set as Background…", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.set_background", pv);
        g_menu_append_item(s3, mi); g_object_unref(mi);
    }

    mi = g_menu_item_new("Move to Trash", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.trash", pv);
    g_menu_append_item(s3, mi); g_object_unref(mi);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s3)); g_object_unref(s3);

    /* Properties */
    GMenu *s4 = g_menu_new();
    mi = g_menu_item_new("Properties", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.properties", pv);
    g_menu_append_item(s4, mi); g_object_unref(mi);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s4)); g_object_unref(s4);

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    GtkWidget *parent_view = gtk_widget_get_ancestor(box, GTK_TYPE_GRID_VIEW);
    if (!parent_view)
        parent_view = gtk_widget_get_ancestor(box, GTK_TYPE_COLUMN_VIEW);

    if (parent_view) {
        gtk_widget_set_parent(popover, parent_view);
        g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
        double xv = 0, yv = 0;
        gtk_widget_translate_coordinates(box, parent_view, x, y, &xv, &yv);
        GdkRectangle rect = { (int)xv, (int)yv, 1, 1 };
        gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
        gtk_popover_popup(GTK_POPOVER(popover));
    } else {
        g_object_unref(popover);
    }
    g_object_unref(menu);
}

/* ── Grid factory ── */
static void setup_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "nautilus-grid-cell");
    gtk_widget_set_size_request(box, 110, 110);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    GtkWidget *icon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);

    GtkWidget *label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 12);
    gtk_label_set_lines(GTK_LABEL(label), 2);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);

    GtkGesture *g = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), GDK_BUTTON_SECONDARY);
    g_signal_connect(g, "pressed", G_CALLBACK(on_item_right_clicked), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(g));

    gtk_list_item_set_child(li, box);
}

static void bind_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *icon  = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(icon);
    gpointer   item  = gtk_list_item_get_item(li);
    if (!item || !AETHER_IS_FILE_ENTITY(item)) return;

    AetherFileEntity *e = AETHER_FILE_ENTITY(item);
    gtk_image_set_from_icon_name(GTK_IMAGE(icon), aether_file_entity_get_icon_name(e));
    gtk_label_set_text(GTK_LABEL(label), aether_file_entity_get_name(e));
}

static void unbind_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *icon  = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(icon);
    gtk_image_clear(GTK_IMAGE(icon));
    gtk_label_set_text(GTK_LABEL(label), "");
}

/* ── List factory ── */
static void setup_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget *icon  = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 20);
    GtkWidget *label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);

    GtkGesture *g = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), GDK_BUTTON_SECONDARY);
    g_signal_connect(g, "pressed", G_CALLBACK(on_item_right_clicked), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(g));

    gtk_list_item_set_child(li, box);
}

static void bind_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *icon  = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(icon);
    gpointer   item  = gtk_list_item_get_item(li);
    if (!item || !AETHER_IS_FILE_ENTITY(item)) return;

    AetherFileEntity *e = AETHER_FILE_ENTITY(item);
    gtk_image_set_from_icon_name(GTK_IMAGE(icon), aether_file_entity_get_icon_name(e));
    gtk_label_set_text(GTK_LABEL(label), aether_file_entity_get_name(e));
}

static void unbind_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *icon  = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(icon);
    gtk_image_clear(GTK_IMAGE(icon));
    gtk_label_set_text(GTK_LABEL(label), "");
}

/* ── Search filter ── */
static gboolean name_filter_func(gpointer item, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    if (!self->filter_string || self->filter_string[0] == '\0') return TRUE;
    AetherFileEntity *e = AETHER_FILE_ENTITY(item);
    const char *name = aether_file_entity_get_name(e);
    if (!name) return FALSE;
    char *name_lower   = g_utf8_casefold(name, -1);
    char *filter_lower = g_utf8_casefold(self->filter_string, -1);
    gboolean match = strstr(name_lower, filter_lower) != NULL;
    g_free(name_lower);
    g_free(filter_lower);
    return match;
}

/* ── Sorting ── */
static gint compare_entities(gconstpointer a, gconstpointer b, gpointer user_data) {
    AetherFileEntity *f1 = AETHER_FILE_ENTITY(a);
    AetherFileEntity *f2 = AETHER_FILE_ENTITY(b);
    gboolean d1 = aether_file_entity_is_directory(f1);
    gboolean d2 = aether_file_entity_is_directory(f2);
    if (d1 && !d2) return GTK_ORDERING_SMALLER;
    if (!d1 && d2) return GTK_ORDERING_LARGER;
    char *k1 = g_utf8_casefold(aether_file_entity_get_name(f1) ?: "", -1);
    char *k2 = g_utf8_casefold(aether_file_entity_get_name(f2) ?: "", -1);
    int cmp = g_utf8_collate(k1, k2);
    g_free(k1); g_free(k2);
    if (cmp < 0) return GTK_ORDERING_SMALLER;
    if (cmp > 0) return GTK_ORDERING_LARGER;
    return GTK_ORDERING_EQUAL;
}

/* ── Status bar update ── */
static void update_statusbar(AetherWindow *self) {
    char *text = g_strdup_printf("%u items", self->item_count);
    gtk_label_set_text(GTK_LABEL(self->status_label), text);
    g_free(text);

    GFile    *f     = g_file_new_for_path(self->current_path ? self->current_path : "/");
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

/* ── Nav button state ── */
static void update_nav_buttons(AetherWindow *self) {
    gtk_widget_set_sensitive(self->btn_back, self->back_stack->len > 0);
    gtk_widget_set_sensitive(self->btn_fwd,  self->fwd_stack->len  > 0);
}

/* ── Directory loaded callback ── */
static void on_directory_loaded(GObject *source, GAsyncResult *res, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GError *err = NULL;
    GListModel *raw = aether_file_repository_list_directory_finish(
                          AETHER_FILE_REPOSITORY(source), res, &err);
    if (err) { g_printerr("Error: %s\n", err->message); g_error_free(err); return; }

    /* Filter hidden files */
    GListStore *visible = g_list_store_new(AETHER_TYPE_FILE_ENTITY);
    guint n = g_list_model_get_n_items(raw);
    self->item_count = 0;
    for (guint i = 0; i < n; i++) {
        AetherFileEntity *e = AETHER_FILE_ENTITY(g_list_model_get_item(raw, i));
        const char *name = aether_file_entity_get_name(e);
        if (!self->show_hidden && name && name[0] == '.') {
            g_object_unref(e);
            continue;
        }
        g_list_store_append(visible, e);
        g_object_unref(e);
        self->item_count++;
    }
    g_object_unref(raw);

    GtkCustomSorter    *sorter     = gtk_custom_sorter_new(compare_entities, NULL, NULL);
    GtkSortListModel   *sorted     = gtk_sort_list_model_new(G_LIST_MODEL(visible), GTK_SORTER(sorter));
    GtkCustomFilter    *filter     = gtk_custom_filter_new(name_filter_func, self, NULL);
    GtkFilterListModel *filtered   = gtk_filter_list_model_new(G_LIST_MODEL(sorted), GTK_FILTER(filter));

    self->filter_model = filtered;
    self->name_filter  = filter;

    GtkMultiSelection *grid_sel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(filtered)));
    gtk_grid_view_set_model(GTK_GRID_VIEW(self->grid_view), GTK_SELECTION_MODEL(grid_sel));
    g_object_unref(grid_sel);

    GtkMultiSelection *list_sel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(filtered)));
    gtk_column_view_set_model(GTK_COLUMN_VIEW(self->list_view), GTK_SELECTION_MODEL(list_sel));
    g_object_unref(list_sel);

    update_statusbar(self);
}

/* ── Path button ── */
static void on_path_button_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(btn), "path");
    if (path) load_directory(self, path);
}

/* ── Update path bar ── */
static void update_pathbar(AetherWindow *self) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->path_bar)) != NULL)
        gtk_box_remove(GTK_BOX(self->path_bar), child);

    char **segs = g_strsplit(self->current_path, "/", -1);
    char  *accum = g_strdup("");

    for (int i = 0; segs[i] != NULL; i++) {
        if (strlen(segs[i]) == 0 && i == 0) continue;

        char *new_path;
        if (strlen(accum) == 0)
            new_path = g_strdup_printf("/%s", segs[i]);
        else
            new_path = g_build_filename(accum, segs[i], NULL);
        g_free(accum);
        accum = new_path;

        const char *label = (strlen(segs[i]) == 0) ? "Root" : segs[i];
        GtkWidget  *btn   = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(btn, "flat");
        g_object_set_data_full(G_OBJECT(btn), "path", g_strdup(accum), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_path_button_clicked), self);
        gtk_box_append(GTK_BOX(self->path_bar), btn);

        if (segs[i+1] && strlen(segs[i+1]) > 0) {
            GtkWidget *sep = gtk_label_new("›");
            gtk_widget_add_css_class(sep, "path-sep");
            gtk_widget_set_margin_start(sep, 2);
            gtk_widget_set_margin_end(sep, 2);
            gtk_box_append(GTK_BOX(self->path_bar), sep);
        }
    }
    g_free(accum);
    g_strfreev(segs);
}

/* ── Core navigate ── */
static void load_directory(AetherWindow *self, const char *path) {
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

static void navigate_back(AetherWindow *self) {
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

static void navigate_forward(AetherWindow *self) {
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

static void on_btn_back_clicked(GtkButton *btn, gpointer user_data) {
    navigate_back(AETHER_WINDOW(user_data));
}
static void on_btn_fwd_clicked(GtkButton *btn, gpointer user_data) {
    navigate_forward(AETHER_WINDOW(user_data));
}

/* ── Item activated ── */
static void on_item_activated(GtkWidget *view, guint position, gpointer user_data) {
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
        const char *uri = aether_file_entity_get_uri(e);
        if (uri) g_app_info_launch_default_for_uri_async(uri, NULL, NULL, NULL, NULL);
    }
    g_object_unref(item);
}

/* ── View toggle ── */
static void on_view_switched(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *name = g_object_get_data(G_OBJECT(btn), "view-name");
    gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), name);
    gtk_widget_remove_css_class(self->btn_grid, "active-view");
    gtk_widget_remove_css_class(self->btn_list, "active-view");
    gtk_widget_add_css_class(GTK_WIDGET(btn), "active-view");
}

/* ── Search ── */
static void on_search_changed(GtkSearchEntry *entry, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    g_free(self->filter_string);
    self->filter_string = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
    if (self->name_filter)
        gtk_filter_changed(GTK_FILTER(self->name_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

/* ── Sidebar ── */
static void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(row), "path");
    if (path) load_directory(self, path);
}

static GtkWidget *make_sidebar_row(const char *name, const char *icon_name) {
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
    return row;
}

static void add_sidebar_place(AetherWindow *self, const char *name,
                               const char *icon, const char *path)
{
    GtkWidget *row = make_sidebar_row(name, icon);
    g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path), g_free);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

static void add_sidebar_header(AetherWindow *self, const char *title) {
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

static void add_sidebar_separator(AetherWindow *self) {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_widget_set_margin_top(sep, 4);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), sep);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

static void setup_sidebar(AetherWindow *self) {
    self->sidebar_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->sidebar_list), GTK_SELECTION_SINGLE);
    g_signal_connect(self->sidebar_list, "row-activated",
                     G_CALLBACK(on_sidebar_row_activated), self);
    gtk_widget_add_css_class(self->sidebar_list, "navigation-sidebar");

    /* Pinned */
    add_sidebar_header(self, "PINNED");
    add_sidebar_place(self, "Recent",    "document-open-recent-symbolic", g_get_home_dir());
    add_sidebar_place(self, "Starred",   "starred-symbolic",              g_get_home_dir());
    add_sidebar_place(self, "Home",      "user-home-symbolic",            g_get_home_dir());

    add_sidebar_separator(self);

    /* Places */
    add_sidebar_header(self, "PLACES");
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
    add_sidebar_place(self, "Trash",     "user-trash-symbolic",           "/tmp");

    add_sidebar_separator(self);

    /* Drives */
    add_sidebar_header(self, "DEVICES");

    GVolumeMonitor *vm = g_volume_monitor_get();
    GList *mounts = g_volume_monitor_get_mounts(vm);
    for (GList *l = mounts; l != NULL; l = l->next) {
        GMount *mount = G_MOUNT(l->data);
        char   *name  = g_mount_get_name(mount);
        GFile  *root  = g_mount_get_root(mount);
        char   *mpath = g_file_get_path(root);
        if (mpath) {
            GtkWidget *row = make_sidebar_row(name, "drive-harddisk-symbolic");
            g_object_set_data_full(G_OBJECT(row), "path", g_strdup(mpath), g_free);
            gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
        }
        g_free(name); g_free(mpath);
        g_object_unref(root);
    }
    g_list_free_full(mounts, g_object_unref);
    g_object_unref(vm);
}

/* ── Toolbar action callbacks ── */
static void on_new_folder_response(AdwAlertDialog *d, const char *response, gpointer ud) {
    (void)ud;
    if (g_strcmp0(response, "create") != 0) return;
    AetherWindow *w   = AETHER_WINDOW(g_object_get_data(G_OBJECT(d), "window"));
    GtkWidget    *ent = GTK_WIDGET(g_object_get_data(G_OBJECT(d), "entry"));
    const char   *nm  = gtk_editable_get_text(GTK_EDITABLE(ent));
    if (!nm || nm[0] == '\0') return;
    char  *full = g_build_filename(w->current_path, nm, NULL);
    GFile *dir  = g_file_new_for_path(full);
    GError *err = NULL;
    g_file_make_directory(dir, NULL, &err);
    if (err) { g_printerr("mkdir error: %s\n", err->message); g_error_free(err); }
    else      { load_directory(w, w->current_path); }
    g_free(full);
    g_object_unref(dir);
}

static void on_new_folder_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    if (!self->current_path) return;

    AdwDialog *dialog = ADW_DIALOG(adw_alert_dialog_new("New Folder", NULL));
    adw_alert_dialog_set_body(ADW_ALERT_DIALOG(dialog), "Enter a name for the new folder:");

    GtkWidget *entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Folder name");
    gtk_editable_set_text(GTK_EDITABLE(entry), "New Folder");
    gtk_editable_select_region(GTK_EDITABLE(entry), 0, -1);
    adw_alert_dialog_set_extra_child(ADW_ALERT_DIALOG(dialog), entry);
    adw_alert_dialog_add_responses(ADW_ALERT_DIALOG(dialog),
                                   "cancel", "Cancel",
                                   "create", "Create",
                                   NULL);
    adw_alert_dialog_set_response_appearance(ADW_ALERT_DIALOG(dialog),
                                              "create", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(ADW_ALERT_DIALOG(dialog), "create");
    adw_alert_dialog_set_close_response(ADW_ALERT_DIALOG(dialog), "cancel");

    /* Store self + entry for the response handler */
    g_object_set_data(G_OBJECT(dialog), "window", self);
    g_object_set_data(G_OBJECT(dialog), "entry",  entry);

    g_signal_connect(dialog, "response",
        G_CALLBACK(on_new_folder_response), NULL);

    adw_dialog_present(dialog, GTK_WIDGET(self));
}

static void on_new_document_clicked(GtkButton *btn, gpointer user_data) {
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
        g_printerr("touch error: %s\n", err->message);
        g_error_free(err);
    }
    g_free(full);
    g_object_unref(file);
}

static void on_open_terminal_clicked(GtkButton *btn, gpointer user_data) {
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

static void on_select_all_clicked(GtkButton *btn, gpointer user_data) {
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

static void on_paste_done(GObject *src, GAsyncResult *res, gpointer ud) {
    AetherWindow *w = AETHER_WINDOW(ud);
    GError *err = NULL;
    aether_clipboard_paste_finish(w->clipboard, res, &err);
    if (err) { g_printerr("Paste error: %s\n", err->message); g_error_free(err); }
    else load_directory(w, w->current_path);
}

static void on_paste_toolbar_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    (void)btn;
    if (!self->current_path) return;
    if (!aether_clipboard_has_content(self->clipboard)) return;
    aether_clipboard_paste(self->clipboard, self->current_path,
        on_paste_done, self);
}

/* ── Toggle hidden files ── */
static void on_toggle_hidden(GSimpleAction *action, GVariant *param, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    self->show_hidden = !self->show_hidden;
    load_directory(self, self->current_path);
}

/* ── Keyboard shortcut helpers (GtkShortcutFunc signature) ── */
static gboolean on_key_back(GtkWidget *w, GVariant *args, gpointer ud) {
    (void)w; (void)args; navigate_back(AETHER_WINDOW(ud)); return TRUE;
}
static gboolean on_key_fwd(GtkWidget *w, GVariant *args, gpointer ud) {
    (void)w; (void)args; navigate_forward(AETHER_WINDOW(ud)); return TRUE;
}
static gboolean on_key_up(GtkWidget *w, GVariant *args, gpointer ud) {
    (void)w; (void)args;
    AetherWindow *self = AETHER_WINDOW(ud);
    if (!self->current_path) return TRUE;
    char *parent = g_path_get_dirname(self->current_path);
    if (g_strcmp0(parent, self->current_path) != 0)
        load_directory(self, parent);
    g_free(parent);
    return TRUE;
}
static gboolean on_key_refresh(GtkWidget *w, GVariant *args, gpointer ud) {
    (void)w; (void)args;
    AetherWindow *self = AETHER_WINDOW(ud);
    if (self->current_path)
        aether_file_repository_list_directory_async(
            self->repo, self->current_path, NULL, on_directory_loaded, self);
    return TRUE;
}

/* ── GObject lifecycle ── */
static void aether_window_dispose(GObject *object) {
    AetherWindow *self = AETHER_WINDOW(object);
    g_clear_object(&self->repo);
    g_free(self->current_path);
    g_free(self->filter_string);
    if (self->back_stack) g_ptr_array_free(self->back_stack, TRUE);
    if (self->fwd_stack)  g_ptr_array_free(self->fwd_stack,  TRUE);
    aether_clipboard_controller_free(self->clipboard);
    G_OBJECT_CLASS(aether_window_parent_class)->dispose(object);
}

static void aether_window_class_init(AetherWindowClass *klass) {
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->dispose = aether_window_dispose;
}

/* ── Window init ── */
static void aether_window_init(AetherWindow *self) {
    self->current_path   = NULL;
    self->filter_string  = NULL;
    self->show_hidden    = FALSE;
    self->item_count     = 0;
    self->back_stack     = g_ptr_array_new_with_free_func(g_free);
    self->fwd_stack      = g_ptr_array_new_with_free_func(g_free);
    self->clipboard      = aether_clipboard_controller_new();

    gtk_window_set_title(GTK_WINDOW(self), "AetherFiles");
    gtk_window_set_default_size(GTK_WINDOW(self), 1000, 660);
    gtk_widget_add_css_class(GTK_WIDGET(self), "aether-window");

    load_css();

    self->repo = AETHER_FILE_REPOSITORY(aether_gio_file_repository_new());

    /* ══ Sidebar ══ */
    setup_sidebar(self);

    GtkWidget *sidebar_toolbar = adw_toolbar_view_new();
    gtk_widget_add_css_class(sidebar_toolbar, "aether-sidebar");

    GtkWidget *sidebar_header = adw_header_bar_new();
    GtkWidget *title_label = gtk_label_new("Files");
    gtk_widget_add_css_class(title_label, "title");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(sidebar_header), title_label);
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(sidebar_header), FALSE);
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(sidebar_header), FALSE);

    GtkWidget *sidebar_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scrolled), self->sidebar_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(sidebar_toolbar), sidebar_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(sidebar_toolbar), sidebar_scrolled);

    /* ══ Views ══ */
    self->view_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->view_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(self->view_stack), 150);

    /* Grid View */
    GtkWidget *grid_scrolled = gtk_scrolled_window_new();
    gtk_widget_add_css_class(grid_scrolled, "aether-grid");
    self->grid_view = gtk_grid_view_new(NULL, NULL);
    gtk_grid_view_set_max_columns(GTK_GRID_VIEW(self->grid_view), 18);
    gtk_grid_view_set_min_columns(GTK_GRID_VIEW(self->grid_view), 2);
    gtk_grid_view_set_enable_rubberband(GTK_GRID_VIEW(self->grid_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(grid_scrolled), self->grid_view);
    g_signal_connect(self->grid_view, "activate", G_CALLBACK(on_item_activated), self);

    GtkListItemFactory *gf = gtk_signal_list_item_factory_new();
    g_signal_connect(gf, "setup",   G_CALLBACK(setup_grid_item),  NULL);
    g_signal_connect(gf, "bind",    G_CALLBACK(bind_grid_item),   NULL);
    g_signal_connect(gf, "unbind",  G_CALLBACK(unbind_grid_item), NULL);
    gtk_grid_view_set_factory(GTK_GRID_VIEW(self->grid_view), gf);
    g_object_unref(gf);

    /* List View */
    GtkWidget *list_scrolled = gtk_scrolled_window_new();
    self->list_view = gtk_column_view_new(NULL);
    gtk_column_view_set_enable_rubberband(GTK_COLUMN_VIEW(self->list_view), TRUE);
    gtk_column_view_set_show_row_separators(GTK_COLUMN_VIEW(self->list_view), FALSE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scrolled), self->list_view);
    g_signal_connect(self->list_view, "activate", G_CALLBACK(on_item_activated), self);

    GtkListItemFactory *lf = gtk_signal_list_item_factory_new();
    g_signal_connect(lf, "setup",   G_CALLBACK(setup_list_item),  NULL);
    g_signal_connect(lf, "bind",    G_CALLBACK(bind_list_item),   NULL);
    g_signal_connect(lf, "unbind",  G_CALLBACK(unbind_list_item), NULL);
    GtkColumnViewColumn *col = gtk_column_view_column_new("Name", lf);
    gtk_column_view_column_set_expand(col, TRUE);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(self->list_view), col);
    g_object_unref(col);

    gtk_stack_add_named(GTK_STACK(self->view_stack), grid_scrolled, "grid");
    gtk_stack_add_named(GTK_STACK(self->view_stack), list_scrolled, "list");

    /* ══ Search bar ══ */
    self->search_entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(self->search_entry), "Search files…");
    g_signal_connect(self->search_entry, "search-changed", G_CALLBACK(on_search_changed), self);

    self->search_bar = gtk_search_bar_new();
    gtk_search_bar_set_child(GTK_SEARCH_BAR(self->search_bar), self->search_entry);
    gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(self->search_bar), TRUE);

    /* ══ Content header ══ */
    GtkWidget *content_header = adw_header_bar_new();
    adw_header_bar_set_show_end_title_buttons(ADW_HEADER_BAR(content_header), TRUE);
    adw_header_bar_set_show_start_title_buttons(ADW_HEADER_BAR(content_header), FALSE);

    /* Nav buttons */
    GtkWidget *nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    self->btn_back = gtk_button_new_from_icon_name("go-previous-symbolic");
    self->btn_fwd  = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_widget_add_css_class(self->btn_back, "nav-btn");
    gtk_widget_add_css_class(self->btn_fwd,  "nav-btn");
    gtk_widget_set_sensitive(self->btn_back, FALSE);
    gtk_widget_set_sensitive(self->btn_fwd,  FALSE);
    g_signal_connect(self->btn_back, "clicked", G_CALLBACK(on_btn_back_clicked), self);
    g_signal_connect(self->btn_fwd,  "clicked", G_CALLBACK(on_btn_fwd_clicked),  self);
    gtk_box_append(GTK_BOX(nav_box), self->btn_back);
    gtk_box_append(GTK_BOX(nav_box), self->btn_fwd);
    adw_header_bar_pack_start(ADW_HEADER_BAR(content_header), nav_box);

    /* Path bar */
    self->path_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(self->path_bar, "aether-pathbar");
    gtk_widget_set_hexpand(self->path_bar, TRUE);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(content_header), self->path_bar);

    /* View switcher */
    GtkWidget *view_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    self->btn_list = gtk_button_new_from_icon_name("view-list-symbolic");
    self->btn_grid = gtk_button_new_from_icon_name("view-grid-symbolic");
    gtk_widget_add_css_class(self->btn_list, "view-switcher-btn");
    gtk_widget_add_css_class(self->btn_grid, "view-switcher-btn");
    gtk_widget_add_css_class(self->btn_grid, "active-view");
    g_object_set_data(G_OBJECT(self->btn_grid), "view-name", "grid");
    g_object_set_data(G_OBJECT(self->btn_list), "view-name", "list");
    g_signal_connect(self->btn_grid, "clicked", G_CALLBACK(on_view_switched), self);
    g_signal_connect(self->btn_list, "clicked", G_CALLBACK(on_view_switched), self);

    /* Search toggle */
    GtkWidget *search_btn = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(search_btn), "system-search-symbolic");
    gtk_widget_add_css_class(search_btn, "flat");
    gtk_widget_set_tooltip_text(search_btn, "Search (Ctrl+F)");
    g_object_bind_property(search_btn, "active",
                            self->search_bar, "search-mode-enabled",
                            G_BINDING_BIDIRECTIONAL);

    gtk_box_append(GTK_BOX(view_box), self->btn_list);
    gtk_box_append(GTK_BOX(view_box), self->btn_grid);
    gtk_box_append(GTK_BOX(view_box), search_btn);
    adw_header_bar_pack_end(ADW_HEADER_BAR(content_header), view_box);

    /* ══ Action toolbar ══ */
    GtkWidget *toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_add_css_class(toolbar, "aether-toolbar");

    struct { const char *icon; const char *label; GCallback cb; } tb_btns[] = {
        { "folder-new-symbolic",    "New Folder",    G_CALLBACK(on_new_folder_clicked) },
        { "document-new-symbolic",  "New Document",  G_CALLBACK(on_new_document_clicked) },
        { "utilities-terminal-symbolic", "Open Terminal", G_CALLBACK(on_open_terminal_clicked) },
        { "edit-paste-symbolic",    "Paste",         G_CALLBACK(on_paste_toolbar_clicked) },
        { "edit-select-all-symbolic","Select All",   G_CALLBACK(on_select_all_clicked) },
    };

    for (int i = 0; i < 5; i++) {
        GtkWidget *b = gtk_button_new();
        GtkWidget *inner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *ic = gtk_image_new_from_icon_name(tb_btns[i].icon);
        GtkWidget *lb = gtk_label_new(tb_btns[i].label);
        gtk_box_append(GTK_BOX(inner), ic);
        gtk_box_append(GTK_BOX(inner), lb);
        gtk_button_set_child(GTK_BUTTON(b), inner);
        g_signal_connect(b, "clicked", tb_btns[i].cb, self);
        gtk_box_append(GTK_BOX(toolbar), b);
    }

    /* ══ Status bar ══ */
    GtkWidget *statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(statusbar, "aether-statusbar");
    self->status_label = gtk_label_new("0 items");
    self->space_label  = gtk_label_new("");
    gtk_widget_set_hexpand(self->status_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(self->status_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(self->space_label),  1.0f);
    gtk_box_append(GTK_BOX(statusbar), self->status_label);
    gtk_box_append(GTK_BOX(statusbar), self->space_label);

    /* ══ Content assembly ══ */
    GtkWidget *content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_append(GTK_BOX(content_area), self->search_bar);
    gtk_box_append(GTK_BOX(content_area), toolbar);
    gtk_widget_set_vexpand(self->view_stack, TRUE);
    gtk_box_append(GTK_BOX(content_area), self->view_stack);
    gtk_box_append(GTK_BOX(content_area), statusbar);

    GtkWidget *content_toolbar = adw_toolbar_view_new();
    gtk_widget_add_css_class(content_toolbar, "aether-content");
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content_toolbar), content_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(content_toolbar), content_area);

    /* ══ Split view ══ */
    self->split_view = adw_overlay_split_view_new();
    adw_overlay_split_view_set_sidebar(ADW_OVERLAY_SPLIT_VIEW(self->split_view), sidebar_toolbar);
    adw_overlay_split_view_set_content(ADW_OVERLAY_SPLIT_VIEW(self->split_view), content_toolbar);
    adw_overlay_split_view_set_max_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(self->split_view), 240);
    adw_overlay_split_view_set_min_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(self->split_view), 200);
    adw_overlay_split_view_set_sidebar_width_fraction(ADW_OVERLAY_SPLIT_VIEW(self->split_view), 0.22);

    /* Root background */
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root_box, "aether-root");
    gtk_widget_set_hexpand(root_box, TRUE);
    gtk_widget_set_vexpand(root_box, TRUE);
    gtk_box_append(GTK_BOX(root_box), self->split_view);

    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self), root_box);

    /* ══ Keyboard shortcuts via ShortcutController ══ */
    GtkShortcutController *sc = GTK_SHORTCUT_CONTROLLER(gtk_shortcut_controller_new());
    gtk_shortcut_controller_set_scope(sc, GTK_SHORTCUT_SCOPE_MANAGED);

    struct { guint key; GdkModifierType mod; GCallback cb; } sc_list[] = {
        { GDK_KEY_Left,  GDK_ALT_MASK,  G_CALLBACK(on_key_back)    },
        { GDK_KEY_Right, GDK_ALT_MASK,  G_CALLBACK(on_key_fwd)     },
        { GDK_KEY_Up,    GDK_ALT_MASK,  G_CALLBACK(on_key_up)      },
        { GDK_KEY_F5,    0,             G_CALLBACK(on_key_refresh)  },
        { 0, 0, NULL }
    };
    for (int i = 0; sc_list[i].key; i++) {
        GtkShortcut *s = gtk_shortcut_new(
            gtk_keyval_trigger_new(sc_list[i].key, sc_list[i].mod),
            gtk_callback_action_new(sc_list[i].cb, self, NULL));
        gtk_shortcut_controller_add_shortcut(sc, s);
    }
    gtk_widget_add_controller(GTK_WIDGET(self), GTK_EVENT_CONTROLLER(sc));

    /* ══ Toggle hidden via window action ══ */
    GSimpleAction *toggle_hidden = g_simple_action_new("toggle-hidden", NULL);
    g_signal_connect(toggle_hidden, "activate", G_CALLBACK(on_toggle_hidden), self);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(toggle_hidden));
    g_object_unref(toggle_hidden);

    load_directory(self, g_get_home_dir());
}

GtkWindow *aether_window_new(AetherApplication *app) {
    return g_object_new(AETHER_TYPE_WINDOW, "application", app, NULL);
}

/* ── Public API ── */
const char *aether_window_get_current_path(AetherWindow *self) {
    return self->current_path;
}

void aether_window_reload(AetherWindow *self) {
    if (!self->current_path) return;
    aether_file_repository_list_directory_async(
        self->repo, self->current_path, NULL, on_directory_loaded, self);
}
