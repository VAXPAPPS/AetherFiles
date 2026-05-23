#include "window.h"
#include "../../data/gio_file_repository.h"

struct _AetherWindow {
    AdwApplicationWindow parent_instance;
    AetherFileRepository *repo;
    
    char *current_path;
    
    GtkWidget *grid_view;
    GtkWidget *list_view;
    GtkWidget *view_stack;
    
    GtkWidget *path_bar;
    GtkWidget *sidebar_list;
    GtkWidget *split_view;
    GtkWidget *btn_grid;
    GtkWidget *btn_list;
};

G_DEFINE_TYPE(AetherWindow, aether_window, ADW_TYPE_APPLICATION_WINDOW)

static void load_directory(AetherWindow *self, const char *path);

static void on_popover_closed(GtkPopover *popover, gpointer user_data) {
    gtk_widget_unparent(GTK_WIDGET(popover));
}

static void on_item_right_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item) return;
    
    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    GtkWidget *box = gtk_list_item_get_child(list_item);
    
    GMenu *menu = g_menu_new();
    const char *path = aether_file_entity_get_path(entity);
    GVariant *path_variant = g_variant_new_string(path);
    
    // Section 1: Open
    GMenu *open_section = g_menu_new();
    GMenuItem *open_item = g_menu_item_new("Open", "app.open");
    g_menu_item_set_action_and_target_value(open_item, "app.open", path_variant);
    g_menu_append_item(open_section, open_item);
    g_object_unref(open_item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(open_section));
    g_object_unref(open_section);
    
    // Section 2: Clipboard (Cut, Copy, Paste)
    GMenu *clipboard_section = g_menu_new();
    GMenuItem *cut_item = g_menu_item_new("Cut", "app.cut");
    g_menu_item_set_action_and_target_value(cut_item, "app.cut", path_variant);
    g_menu_append_item(clipboard_section, cut_item);
    g_object_unref(cut_item);
    
    GMenuItem *copy_item = g_menu_item_new("Copy", "app.copy");
    g_menu_item_set_action_and_target_value(copy_item, "app.copy", path_variant);
    g_menu_append_item(clipboard_section, copy_item);
    g_object_unref(copy_item);
    
    GMenuItem *paste_item = g_menu_item_new("Paste", "app.paste");
    g_menu_item_set_action_and_target_value(paste_item, "app.paste", path_variant);
    g_menu_append_item(clipboard_section, paste_item);
    g_object_unref(paste_item);
    
    g_menu_append_section(menu, NULL, G_MENU_MODEL(clipboard_section));
    g_object_unref(clipboard_section);
    
    // Section 3: File Management (Rename, Trash, Wallpaper)
    GMenu *manage_section = g_menu_new();
    GMenuItem *rename_item = g_menu_item_new("Rename...", "app.rename");
    g_menu_item_set_action_and_target_value(rename_item, "app.rename", path_variant);
    g_menu_append_item(manage_section, rename_item);
    g_object_unref(rename_item);
    
    if (!aether_file_entity_is_directory(entity)) {
        GMenuItem *bg_item = g_menu_item_new("Set as Background...", "app.set_background");
        g_menu_item_set_action_and_target_value(bg_item, "app.set_background", path_variant);
        g_menu_append_item(manage_section, bg_item);
        g_object_unref(bg_item);
    }
    
    GMenuItem *trash_item = g_menu_item_new("Move to Trash", "app.trash");
    g_menu_item_set_action_and_target_value(trash_item, "app.trash", path_variant);
    g_menu_append_item(manage_section, trash_item);
    g_object_unref(trash_item);
    
    g_menu_append_section(menu, NULL, G_MENU_MODEL(manage_section));
    g_object_unref(manage_section);
    
    // Section 4: Properties
    GMenu *props_section = g_menu_new();
    GMenuItem *props_item = g_menu_item_new("Properties", "app.properties");
    g_menu_item_set_action_and_target_value(props_item, "app.properties", path_variant);
    g_menu_append_item(props_section, props_item);
    g_object_unref(props_item);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(props_section));
    g_object_unref(props_section);
    
    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    GtkWidget *parent_view = gtk_widget_get_ancestor(box, GTK_TYPE_GRID_VIEW);
    if (!parent_view) {
        parent_view = gtk_widget_get_ancestor(box, GTK_TYPE_COLUMN_VIEW);
    }
    
    if (parent_view) {
        gtk_widget_set_parent(popover, parent_view);
        g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
        
        double x_view = 0, y_view = 0;
        if (gtk_widget_translate_coordinates(box, parent_view, x, y, &x_view, &y_view)) {
            GdkRectangle rect = { (int)x_view, (int)y_view, 1, 1 };
            gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
        }
        
        gtk_popover_popup(GTK_POPOVER(popover));
    } else {
        g_object_unref(popover);
    }
    
    g_object_unref(menu);
}

static void setup_list_item(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer data) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *icon = gtk_image_new();
    gtk_image_set_icon_size(GTK_IMAGE(icon), GTK_ICON_SIZE_LARGE);
    GtkWidget *label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 12);
    gtk_label_set_lines(GTK_LABEL(label), 2);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    
    // Add Nautilus cell-like styling
    gtk_widget_add_css_class(box, "nautilus-grid-cell");
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    
    // Size constraints similar to Nautilus
    gtk_widget_set_size_request(box, 110, 110);
    
    GtkGesture *gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_item_right_clicked), list_item);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(gesture));
    
    gtk_list_item_set_child(list_item, box);
}

static void bind_list_item(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer data) {
    GtkWidget *box = gtk_list_item_get_child(list_item);
    if (!box) return;
    
    GtkWidget *icon = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(icon);
    
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item || !AETHER_IS_FILE_ENTITY(item)) return;
    
    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    
    const char *icon_name = aether_file_entity_get_icon_name(entity);
    if (icon_name) {
        gtk_image_set_from_icon_name(GTK_IMAGE(icon), icon_name);
    }
    
    const char *name = aether_file_entity_get_name(entity);
    if (name) {
        gtk_label_set_text(GTK_LABEL(label), name);
    }
}

static void unbind_list_item(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer data) {
    GtkWidget *box = gtk_list_item_get_child(list_item);
    if (!box) return;
    GtkWidget *icon = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(icon);
    gtk_image_clear(GTK_IMAGE(icon));
    gtk_label_set_text(GTK_LABEL(label), "");
}

static void on_item_activated(GtkWidget *view, guint position, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GtkSelectionModel *selection = NULL;
    
    if (GTK_IS_GRID_VIEW(view)) {
        selection = gtk_grid_view_get_model(GTK_GRID_VIEW(view));
    } else if (GTK_IS_COLUMN_VIEW(view)) {
        selection = gtk_column_view_get_model(GTK_COLUMN_VIEW(view));
    }
    
    if (!selection) return;
    
    gpointer item = g_list_model_get_item(G_LIST_MODEL(selection), position);
    if (!item) return;
    
    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    if (aether_file_entity_is_directory(entity)) {
        load_directory(self, aether_file_entity_get_path(entity));
    } else {
        const char *uri = aether_file_entity_get_uri(entity);
        if (uri) {
            g_app_info_launch_default_for_uri_async(uri, NULL, NULL, NULL, NULL);
        }
    }
    
    g_object_unref(item);
}

static gint compare_file_entities(gconstpointer a, gconstpointer b, gpointer user_data) {
    AetherFileEntity *f1 = AETHER_FILE_ENTITY(a);
    AetherFileEntity *f2 = AETHER_FILE_ENTITY(b);
    gboolean d1 = aether_file_entity_is_directory(f1);
    gboolean d2 = aether_file_entity_is_directory(f2);
    if (d1 && !d2) return GTK_ORDERING_SMALLER;
    if (!d1 && d2) return GTK_ORDERING_LARGER;
    
    const char *n1 = aether_file_entity_get_name(f1);
    const char *n2 = aether_file_entity_get_name(f2);
    char *k1 = g_utf8_casefold(n1 ? n1 : "", -1);
    char *k2 = g_utf8_casefold(n2 ? n2 : "", -1);
    int cmp = g_utf8_collate(k1, k2);
    g_free(k1);
    g_free(k2);
    
    if (cmp < 0) return GTK_ORDERING_SMALLER;
    if (cmp > 0) return GTK_ORDERING_LARGER;
    return GTK_ORDERING_EQUAL;
}

static void on_directory_loaded(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GError *error = NULL;
    GListModel *model = aether_file_repository_list_directory_finish(AETHER_FILE_REPOSITORY(source_object), res, &error);
    
    if (error) {
        g_printerr("Failed to load directory: %s\n", error->message);
        g_error_free(error);
        return;
    }
    
    GtkCustomSorter *sorter = gtk_custom_sorter_new(compare_file_entities, NULL, NULL);
    GtkSortListModel *sort_model = gtk_sort_list_model_new(model, GTK_SORTER(sorter));
    
    // gtk_multi_selection_new consumes the reference to the model, so we must ref it for the second one
    GtkMultiSelection *grid_selection = gtk_multi_selection_new(G_LIST_MODEL(sort_model));
    gtk_grid_view_set_model(GTK_GRID_VIEW(self->grid_view), GTK_SELECTION_MODEL(grid_selection));
    g_object_unref(grid_selection);
    
    // GtkColumnView needs its own selection model to avoid focus issues
    GtkMultiSelection *list_selection = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(sort_model)));
    gtk_column_view_set_model(GTK_COLUMN_VIEW(self->list_view), GTK_SELECTION_MODEL(list_selection));
    g_object_unref(list_selection);
}

static void on_path_button_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(btn), "path");
    if (path) {
        load_directory(self, path);
    }
}

static void on_view_switched(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *view_name = g_object_get_data(G_OBJECT(btn), "view-name");
    
    gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), view_name);
    
    gtk_widget_remove_css_class(self->btn_grid, "suggested-action");
    gtk_widget_remove_css_class(self->btn_list, "suggested-action");
    gtk_widget_add_css_class(GTK_WIDGET(btn), "suggested-action");
}

static void update_pathbar(AetherWindow *self) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->path_bar)) != NULL) {
        gtk_box_remove(GTK_BOX(self->path_bar), child);
    }
    
    char **segments = g_strsplit(self->current_path, "/", -1);
    char *accumulated_path = g_strdup("");
    gtk_widget_add_css_class(self->path_bar, "linked");
    
    for (int i = 0; segments[i] != NULL; i++) {
        if (strlen(segments[i]) == 0 && i == 0) continue; // Skip initial empty segment for root
        
        char *new_path;
        if (strlen(accumulated_path) == 0) {
            new_path = g_strdup_printf("/%s", segments[i]);
        } else {
            new_path = g_build_filename(accumulated_path, segments[i], NULL);
        }
        g_free(accumulated_path);
        accumulated_path = new_path;
        
        const char *label_text = (strlen(segments[i]) == 0) ? "Root" : segments[i];
        
        GtkWidget *btn = gtk_button_new_with_label(label_text);
        g_object_set_data_full(G_OBJECT(btn), "path", g_strdup(accumulated_path), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_path_button_clicked), self);
        
        gtk_box_append(GTK_BOX(self->path_bar), btn);
        
        if (segments[i+1] != NULL && strlen(segments[i+1]) > 0) {
            GtkWidget *sep = gtk_label_new(">");
            gtk_widget_set_margin_start(sep, 4);
            gtk_widget_set_margin_end(sep, 4);
            gtk_box_append(GTK_BOX(self->path_bar), sep);
        }
    }
    
    g_free(accumulated_path);
    g_strfreev(segments);
}

static void load_directory(AetherWindow *self, const char *path) {
    g_free(self->current_path);
    self->current_path = g_strdup(path);
    
    update_pathbar(self);
    
    aether_file_repository_list_directory_async(self->repo, self->current_path, NULL, on_directory_loaded, self);
}

static void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(row), "path");
    if (path) {
        load_directory(self, path);
    }
}

static void add_sidebar_place(AetherWindow *self, const char *name, const char *icon_name, const char *path) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    
    GtkWidget *icon = gtk_image_new_from_icon_name(icon_name);
    GtkWidget *label = gtk_label_new(name);
    
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    
    g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path), g_free);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

static void setup_sidebar(AetherWindow *self) {
    self->sidebar_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->sidebar_list), GTK_SELECTION_SINGLE);
    g_signal_connect(self->sidebar_list, "row-activated", G_CALLBACK(on_sidebar_row_activated), self);
    gtk_widget_add_css_class(self->sidebar_list, "navigation-sidebar");
    
    add_sidebar_place(self, "Home", "user-home", g_get_home_dir());
    add_sidebar_place(self, "Desktop", "user-desktop", g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP));
    add_sidebar_place(self, "Documents", "folder-documents", g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS));
    add_sidebar_place(self, "Downloads", "folder-download", g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD));
    add_sidebar_place(self, "Music", "folder-music", g_get_user_special_dir(G_USER_DIRECTORY_MUSIC));
    add_sidebar_place(self, "Pictures", "folder-pictures", g_get_user_special_dir(G_USER_DIRECTORY_PICTURES));
    add_sidebar_place(self, "Videos", "folder-videos", g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS));
}

static void aether_window_dispose(GObject *object) {
    AetherWindow *self = AETHER_WINDOW(object);
    g_clear_object(&self->repo);
    g_free(self->current_path);
    G_OBJECT_CLASS(aether_window_parent_class)->dispose(object);
}

static void aether_window_class_init(AetherWindowClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = aether_window_dispose;
}

static void setup_list_view_factory(GtkSignalListItemFactory *factory, GtkListItem *list_item, gpointer data) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *icon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);
    GtkWidget *label = gtk_label_new("");
    
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_item_set_child(list_item, box);
    
    GtkGesture *gesture = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
    g_signal_connect(gesture, "pressed", G_CALLBACK(on_item_right_clicked), list_item);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(gesture));
}

static void aether_window_init(AetherWindow *self) {
    self->current_path = NULL;
    
    gtk_window_set_title(GTK_WINDOW(self), "AetherFiles");
    gtk_window_set_default_size(GTK_WINDOW(self), 900, 600);
    
    self->repo = AETHER_FILE_REPOSITORY(aether_gio_file_repository_new());
    
    // Sidebar setup
    setup_sidebar(self);
    
    // Content setup
    self->path_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_margin_top(self->path_bar, 8);
    gtk_widget_set_margin_bottom(self->path_bar, 8);
    gtk_widget_set_margin_start(self->path_bar, 8);
    
    self->view_stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->view_stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    
    // Grid View
    GtkWidget *grid_scrolled = gtk_scrolled_window_new();
    self->grid_view = gtk_grid_view_new(NULL, NULL);
    gtk_grid_view_set_max_columns(GTK_GRID_VIEW(self->grid_view), 20);
    gtk_grid_view_set_min_columns(GTK_GRID_VIEW(self->grid_view), 3);
    gtk_grid_view_set_enable_rubberband(GTK_GRID_VIEW(self->grid_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(grid_scrolled), self->grid_view);
    g_signal_connect(self->grid_view, "activate", G_CALLBACK(on_item_activated), self);
    
    GtkListItemFactory *grid_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(grid_factory, "setup", G_CALLBACK(setup_list_item), NULL);
    g_signal_connect(grid_factory, "bind", G_CALLBACK(bind_list_item), NULL);
    g_signal_connect(grid_factory, "unbind", G_CALLBACK(unbind_list_item), NULL);
    gtk_grid_view_set_factory(GTK_GRID_VIEW(self->grid_view), grid_factory);
    g_object_unref(grid_factory);
    
    // List View
    GtkWidget *list_scrolled = gtk_scrolled_window_new();
    self->list_view = gtk_column_view_new(NULL);
    gtk_column_view_set_enable_rubberband(GTK_COLUMN_VIEW(self->list_view), TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(list_scrolled), self->list_view);
    g_signal_connect(self->list_view, "activate", G_CALLBACK(on_item_activated), self);
    
    GtkListItemFactory *list_factory = gtk_signal_list_item_factory_new();
    g_signal_connect(list_factory, "setup", G_CALLBACK(setup_list_view_factory), NULL);
    g_signal_connect(list_factory, "bind", G_CALLBACK(bind_list_item), NULL);
    g_signal_connect(list_factory, "unbind", G_CALLBACK(unbind_list_item), NULL);
    
    GtkColumnViewColumn *name_col = gtk_column_view_column_new("Name", list_factory);
    gtk_column_view_column_set_expand(name_col, TRUE);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(self->list_view), name_col);
    g_object_unref(name_col);
    
    gtk_stack_add_named(GTK_STACK(self->view_stack), grid_scrolled, "grid");
    gtk_stack_add_named(GTK_STACK(self->view_stack), list_scrolled, "list");
    
    // Libadwaita Layout Structure
    self->split_view = adw_overlay_split_view_new();
    
    // Sidebar Setup
    GtkWidget *sidebar_toolbar = adw_toolbar_view_new();
    GtkWidget *sidebar_header = adw_header_bar_new();
    
    GtkWidget *title_label = gtk_label_new("Files");
    gtk_widget_add_css_class(title_label, "title");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(sidebar_header), title_label);
    
    GtkWidget *search_btn = gtk_button_new_from_icon_name("system-search-symbolic");
    gtk_widget_add_css_class(search_btn, "flat");
    adw_header_bar_pack_start(ADW_HEADER_BAR(sidebar_header), search_btn);
    
    GtkWidget *menu_btn = gtk_button_new_from_icon_name("open-menu-symbolic");
    gtk_widget_add_css_class(menu_btn, "flat");
    adw_header_bar_pack_end(ADW_HEADER_BAR(sidebar_header), menu_btn);
    
    GtkWidget *sidebar_scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scrolled), self->sidebar_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(sidebar_toolbar), sidebar_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(sidebar_toolbar), sidebar_scrolled);
    
    adw_overlay_split_view_set_sidebar(ADW_OVERLAY_SPLIT_VIEW(self->split_view), sidebar_toolbar);
    
    // Main Content Setup
    GtkWidget *content_toolbar = adw_toolbar_view_new();
    GtkWidget *content_header = adw_header_bar_new();
    
    GtkWidget *nav_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(nav_box, "linked");
    GtkWidget *btn_back = gtk_button_new_from_icon_name("go-previous-symbolic");
    GtkWidget *btn_fwd = gtk_button_new_from_icon_name("go-next-symbolic");
    gtk_box_append(GTK_BOX(nav_box), btn_back);
    gtk_box_append(GTK_BOX(nav_box), btn_fwd);
    adw_header_bar_pack_start(ADW_HEADER_BAR(content_header), nav_box);
    
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(content_header), self->path_bar);
    
    // View Switcher (Grid/List) placeholder like Nautilus
    GtkWidget *view_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(view_box, "linked");
    
    self->btn_grid = gtk_button_new_from_icon_name("view-grid-symbolic");
    self->btn_list = gtk_button_new_from_icon_name("view-list-symbolic");
    
    g_object_set_data(G_OBJECT(self->btn_grid), "view-name", "grid");
    g_object_set_data(G_OBJECT(self->btn_list), "view-name", "list");
    g_signal_connect(self->btn_grid, "clicked", G_CALLBACK(on_view_switched), self);
    g_signal_connect(self->btn_list, "clicked", G_CALLBACK(on_view_switched), self);
    
    gtk_widget_add_css_class(self->btn_grid, "suggested-action"); // active view
    
    gtk_box_append(GTK_BOX(view_box), self->btn_list);
    gtk_box_append(GTK_BOX(view_box), self->btn_grid);
    adw_header_bar_pack_end(ADW_HEADER_BAR(content_header), view_box);
    
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content_toolbar), content_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(content_toolbar), self->view_stack);
    
    adw_overlay_split_view_set_content(ADW_OVERLAY_SPLIT_VIEW(self->split_view), content_toolbar);
    
    adw_overlay_split_view_set_max_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(self->split_view), 240);
    adw_overlay_split_view_set_min_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(self->split_view), 200);
    
    // Add split view to window
    adw_application_window_set_content(ADW_APPLICATION_WINDOW(self), self->split_view);
    
    load_directory(self, g_get_home_dir());
}

GtkWindow *aether_window_new(AetherApplication *app) {
    return g_object_new(AETHER_TYPE_WINDOW, "application", app, NULL);
}
