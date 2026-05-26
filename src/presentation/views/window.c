#include "window.h"
#include "window_private.h"

G_DEFINE_TYPE(AetherWindow, aether_window, GTK_TYPE_APPLICATION_WINDOW)

static void undo_entry_free(gpointer p) {
    UndoEntry *e = p;
    g_free(e->src);
    g_free(e->dest);
    g_free(e);
}


static void load_css(void) {
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_resource(provider, "/com/aetheros/files/style.css");
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);
}


static void aether_window_dispose(GObject *object) {
    AetherWindow *self = AETHER_WINDOW(object);
    g_clear_object(&self->repo);
    g_clear_object(&self->drive_mgr);
    g_clear_object(&self->app_repo);
    if (self->dir_monitor) {
        g_file_monitor_cancel(self->dir_monitor);
        g_clear_object(&self->dir_monitor);
    }
    g_free(self->current_path);
    g_free(self->filter_string);
    if (self->back_stack) g_ptr_array_free(self->back_stack, TRUE);
    if (self->fwd_stack)  g_ptr_array_free(self->fwd_stack,  TRUE);
    if (self->undo_stack) g_ptr_array_free(self->undo_stack, TRUE);
    if (self->redo_stack) g_ptr_array_free(self->redo_stack, TRUE);
    if (self->tabs) {
        for (guint i = 0; i < self->tabs->len; i++)
            tab_session_free_fields(&g_array_index(self->tabs, AetherTabSession, i));
        g_array_free(self->tabs, TRUE);
    }
    aether_clipboard_controller_free(self->clipboard);
    G_OBJECT_CLASS(aether_window_parent_class)->dispose(object);
}


static void aether_window_class_init(AetherWindowClass *klass) {
    GObjectClass *oc = G_OBJECT_CLASS(klass);
    oc->dispose = aether_window_dispose;
}

static void on_hidden_toggled(GtkToggleButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    self->show_hidden = gtk_toggle_button_get_active(btn);
    
    if (self->name_filter) {
        gtk_filter_changed(GTK_FILTER(self->name_filter), GTK_FILTER_CHANGE_DIFFERENT);
    }
    update_statusbar(self);
}



static void aether_window_init(AetherWindow *self) {
    self->current_path   = NULL;
    self->filter_string  = NULL;
    self->show_hidden    = FALSE;
    self->item_count     = 0;
    self->sort_mode      = 0;
    self->sort_asc       = TRUE;
    self->back_stack     = g_ptr_array_new_with_free_func(g_free);
    self->fwd_stack      = g_ptr_array_new_with_free_func(g_free);
    self->undo_stack     = g_ptr_array_new_with_free_func(undo_entry_free);
    self->redo_stack     = g_ptr_array_new_with_free_func(undo_entry_free);
    self->clipboard      = aether_clipboard_controller_new();

    /* Tabs array */
    self->tabs = g_array_new(FALSE, TRUE, sizeof(AetherTabSession));

    gtk_window_set_title(GTK_WINDOW(self), "AetherFiles");
    gtk_window_set_default_size(GTK_WINDOW(self), 1000, 660);
    gtk_widget_add_css_class(GTK_WIDGET(self), "aether-window");

    load_css();

    self->repo = AETHER_FILE_REPOSITORY(aether_gio_file_repository_new());
    self->drive_mgr = aether_drive_manager_new();
    self->app_repo = aether_app_repository_new();

    /* ══ Sidebar ══ */
    setup_sidebar(self);

    GtkWidget *sidebar_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar_box, "aether-sidebar");

    GtkWidget *sidebar_header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(sidebar_header), FALSE);

    GtkWidget *sidebar_icons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_halign(sidebar_icons, GTK_ALIGN_CENTER);

    /* 1. More actions */
    GtkWidget *more_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(more_btn), "view-more-symbolic");
    gtk_widget_add_css_class(more_btn, "flat");

    /* 2. Sort */
    GtkWidget *sort_popover = gtk_popover_new();
    GtkWidget *sort_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_start(sort_box, 12);
    gtk_widget_set_margin_end(sort_box, 12);
    gtk_widget_set_margin_top(sort_box, 12);
    gtk_widget_set_margin_bottom(sort_box, 12);

    const char *sort_opts[] = {"Name", "Size", "Type", "Date Modified", NULL};
    GtkWidget *dropdown = gtk_drop_down_new_from_strings(sort_opts);
    g_signal_connect(dropdown, "notify::selected", G_CALLBACK(on_sort_mode_changed), self);
    GtkWidget *dir_btn = gtk_button_new_with_label("Reverse Order");
    g_signal_connect(dir_btn, "clicked", G_CALLBACK(on_sort_dir_clicked), self);

    gtk_box_append(GTK_BOX(sort_box), dropdown);
    gtk_box_append(GTK_BOX(sort_box), dir_btn);
    gtk_popover_set_child(GTK_POPOVER(sort_popover), sort_box);

    self->sort_btn = gtk_menu_button_new();
    gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(self->sort_btn), self->sort_asc ? "view-sort-ascending-symbolic" : "view-sort-descending-symbolic");
    gtk_menu_button_set_popover(GTK_MENU_BUTTON(self->sort_btn), sort_popover);
    gtk_widget_add_css_class(self->sort_btn, "flat");

    /* 3. Show hidden files */
    self->btn_hidden = gtk_toggle_button_new();
    gtk_button_set_icon_name(GTK_BUTTON(self->btn_hidden), "view-reveal-symbolic");
    gtk_widget_add_css_class(self->btn_hidden, "flat");
    g_signal_connect(self->btn_hidden, "toggled", G_CALLBACK(on_hidden_toggled), self);

    /* 4. Circular Progress Indicator */
    self->progress_spinner = gtk_spinner_new();
    gtk_widget_set_margin_start(self->progress_spinner, 4);
    gtk_widget_set_margin_end(self->progress_spinner, 4);

    gtk_box_append(GTK_BOX(sidebar_icons), more_btn);
    gtk_box_append(GTK_BOX(sidebar_icons), self->sort_btn);
    gtk_box_append(GTK_BOX(sidebar_icons), self->btn_hidden);
    gtk_box_append(GTK_BOX(sidebar_icons), self->progress_spinner);

    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(sidebar_header), sidebar_icons);

    GtkWidget *sidebar_scrolled = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(sidebar_scrolled, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(sidebar_scrolled), self->sidebar_list);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(sidebar_scrolled),
                                   GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    gtk_box_append(GTK_BOX(sidebar_box), sidebar_header);
    gtk_box_append(GTK_BOX(sidebar_box), sidebar_scrolled);

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
    GtkGesture *bg_click_grid = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(bg_click_grid), GDK_BUTTON_SECONDARY);
    g_signal_connect(bg_click_grid, "pressed", G_CALLBACK(on_background_right_clicked), self);
    gtk_widget_add_controller(self->grid_view, GTK_EVENT_CONTROLLER(bg_click_grid));

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
    GtkGesture *bg_click_list = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(bg_click_list), GDK_BUTTON_SECONDARY);
    g_signal_connect(bg_click_list, "pressed", G_CALLBACK(on_background_right_clicked), self);
    gtk_widget_add_controller(self->list_view, GTK_EVENT_CONTROLLER(bg_click_list));

    GtkListItemFactory *lf = gtk_signal_list_item_factory_new();
    g_signal_connect(lf, "setup",   G_CALLBACK(setup_list_item),  NULL);
    g_signal_connect(lf, "bind",    G_CALLBACK(bind_list_item),   NULL);
    g_signal_connect(lf, "unbind",  G_CALLBACK(unbind_list_item), NULL);
    GtkColumnViewColumn *col = gtk_column_view_column_new("Name", lf);
    gtk_column_view_column_set_expand(col, TRUE);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(self->list_view), col);
    g_object_unref(col);

    /* Size column */
    GtkListItemFactory *sf = gtk_signal_list_item_factory_new();
    g_signal_connect(sf, "setup",  G_CALLBACK(setup_size_col_item),  NULL);
    g_signal_connect(sf, "bind",   G_CALLBACK(bind_size_col_item),   NULL);
    g_signal_connect(sf, "unbind", G_CALLBACK(unbind_size_col_item), NULL);
    GtkColumnViewColumn *size_col = gtk_column_view_column_new("Size", sf);
    gtk_column_view_column_set_fixed_width(size_col, 90);
    gtk_column_view_append_column(GTK_COLUMN_VIEW(self->list_view), size_col);
    g_object_unref(size_col);

    gtk_stack_add_named(GTK_STACK(self->view_stack), grid_scrolled, "grid");
    gtk_stack_add_named(GTK_STACK(self->view_stack), list_scrolled, "list");

    /* Apps View */
    self->apps_view = setup_apps_view(self);
    gtk_stack_add_named(GTK_STACK(self->view_stack), self->apps_view, "apps");

    /* ══ Search bar ══ */
    self->search_entry = gtk_search_entry_new();
    gtk_search_entry_set_placeholder_text(GTK_SEARCH_ENTRY(self->search_entry), "Search files…");
    gtk_widget_set_hexpand(self->search_entry, TRUE);
    g_signal_connect(self->search_entry, "search-changed", G_CALLBACK(on_search_changed), self);

    const char *filter_opts[] = {"All", "Media", "Document", "Folder", "Apps", "Archive", NULL};
    GtkWidget *filter_dropdown = gtk_drop_down_new_from_strings(filter_opts);
    gtk_widget_add_css_class(filter_dropdown, "flat");
    g_signal_connect(filter_dropdown, "notify::selected", G_CALLBACK(on_search_filter_changed), self);

    GtkWidget *search_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(search_box, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(search_box, 400, -1);
    gtk_box_append(GTK_BOX(search_box), self->search_entry);
    gtk_box_append(GTK_BOX(search_box), filter_dropdown);

    self->search_bar = gtk_search_bar_new();
    gtk_search_bar_set_child(GTK_SEARCH_BAR(self->search_bar), search_box);
    gtk_search_bar_connect_entry(GTK_SEARCH_BAR(self->search_bar), GTK_EDITABLE(self->search_entry));
    gtk_search_bar_set_show_close_button(GTK_SEARCH_BAR(self->search_bar), TRUE);

    /* ══ Content header ══ */
    GtkWidget *content_header = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(content_header), TRUE);

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
    gtk_header_bar_pack_start(GTK_HEADER_BAR(content_header), nav_box);

    /* Path bar */
    self->path_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(self->path_bar, "aether-pathbar");
    gtk_widget_set_hexpand(self->path_bar, TRUE);
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(content_header), self->path_bar);

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
    g_signal_connect(self->search_bar, "notify::search-mode-enabled", G_CALLBACK(on_search_mode_toggled), self);

    gtk_box_append(GTK_BOX(view_box), self->btn_list);
    gtk_box_append(GTK_BOX(view_box), self->btn_grid);
    gtk_box_append(GTK_BOX(view_box), search_btn);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(content_header), view_box);

    /* Remove self->sort_btn assignment here since we already created it */    /* ══ Status bar ══ */
    GtkWidget *statusbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_add_css_class(statusbar, "aether-statusbar");
    self->status_label = gtk_label_new("0 items");
    self->space_label  = gtk_label_new("");
    gtk_widget_set_hexpand(self->status_label, TRUE);
    gtk_label_set_xalign(GTK_LABEL(self->status_label), 0.0f);
    gtk_label_set_xalign(GTK_LABEL(self->space_label),  1.0f);
    gtk_box_append(GTK_BOX(statusbar), self->status_label);
    gtk_box_append(GTK_BOX(statusbar), self->space_label);

    /* ══ GtkNotebook ══ */
    self->tab_view = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(self->tab_view), TRUE);
    gtk_widget_add_css_class(self->tab_view, "aether-tab-view");
    gtk_widget_set_vexpand(self->tab_view, FALSE);
    gtk_widget_set_hexpand(self->tab_view, TRUE);
    gtk_widget_set_visible(self->tab_view, FALSE); /* Hide the tab bar and its empty content area */
    g_signal_connect(self->tab_view, "switch-page",
                     G_CALLBACK(on_tab_selected), self);

    /* ══ Content assembly ══ */
    GtkWidget *content_area = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_vexpand(content_area, TRUE);
    gtk_box_append(GTK_BOX(content_area), self->tab_view);
    gtk_box_append(GTK_BOX(content_area), self->search_bar);
    gtk_widget_set_vexpand(self->view_stack, TRUE);
    gtk_box_append(GTK_BOX(content_area), self->view_stack);
    gtk_box_append(GTK_BOX(content_area), statusbar);

    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(content_box, "aether-content");
    gtk_widget_set_hexpand(content_box, TRUE);
    gtk_box_append(GTK_BOX(content_box), content_header);
    gtk_box_append(GTK_BOX(content_box), content_area);

    /* ══ Split view ══ */
    self->split_view = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_paned_set_start_child(GTK_PANED(self->split_view), sidebar_box);
    gtk_paned_set_end_child(GTK_PANED(self->split_view), content_box);
    gtk_paned_set_position(GTK_PANED(self->split_view), 240);

    /* Root background */
    GtkWidget *root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(root_box, "aether-root");
    gtk_widget_set_hexpand(root_box, TRUE);
    gtk_widget_set_vexpand(root_box, TRUE);
    gtk_box_append(GTK_BOX(root_box), self->split_view);

    gtk_window_set_child(GTK_WINDOW(self), root_box);

    /* ══ Keyboard shortcuts via ShortcutController ══ */
    GtkShortcutController *sc = GTK_SHORTCUT_CONTROLLER(gtk_shortcut_controller_new());
    gtk_shortcut_controller_set_scope(sc, GTK_SHORTCUT_SCOPE_MANAGED);

    struct { guint key; GdkModifierType mod; GCallback cb; } sc_list[] = {
        { GDK_KEY_Left,  GDK_ALT_MASK,              G_CALLBACK(on_key_back)         },
        { GDK_KEY_Right, GDK_ALT_MASK,              G_CALLBACK(on_key_fwd)          },
        { GDK_KEY_Up,    GDK_ALT_MASK,              G_CALLBACK(on_key_up)           },
        { GDK_KEY_F5,    0,                         G_CALLBACK(on_key_refresh)      },
        { GDK_KEY_t,     GDK_CONTROL_MASK,          G_CALLBACK(on_new_tab_shortcut) },
        { GDK_KEY_w,     GDK_CONTROL_MASK,          G_CALLBACK(on_close_tab_shortcut) },
        { GDK_KEY_z,     GDK_CONTROL_MASK,          G_CALLBACK(on_undo_shortcut)    },
        { GDK_KEY_h,     GDK_CONTROL_MASK,          G_CALLBACK(on_ctrl_h_shortcut)  },
        { GDK_KEY_d,     GDK_CONTROL_MASK,          G_CALLBACK(on_ctrl_d_shortcut)  },
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

    /* ══ Drag & Drop ══ */
    setup_drag_drop(self, self->grid_view);
    setup_drag_drop(self, self->list_view);

    /* ══ Bookmarks ══ */
    self->bookmark_row_start = -1; /* will be set in load_bookmarks */
    {
        /* Count existing sidebar rows to know offset for bookmarks */
        int n = 0;
        while (gtk_list_box_get_row_at_index(GTK_LIST_BOX(self->sidebar_list), n) != NULL)
            n++;
        self->bookmark_row_start = n;
    }
    self->bookmark_row_start += 2; /* account for sep + header */
    load_bookmarks(self);

    /* ══ Add bookmark action ══ */
    GSimpleAction *add_bm = g_simple_action_new("add-bookmark", NULL);
    g_signal_connect(add_bm, "activate", G_CALLBACK(on_add_bookmark_action), self);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(add_bm));
    g_object_unref(add_bm);

    GSimpleAction *add_bm_path = g_simple_action_new("add-bookmark-path", G_VARIANT_TYPE_STRING);
    g_signal_connect(add_bm_path, "activate", G_CALLBACK(on_add_bookmark_path_action), self);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(add_bm_path));
    g_object_unref(add_bm_path);

    GSimpleAction *rm_bm = g_simple_action_new("remove-bookmark", NULL);
    g_signal_connect(rm_bm, "activate", G_CALLBACK(on_remove_bookmark_action), self);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(rm_bm));
    g_object_unref(rm_bm);

    GSimpleAction *rm_bm_path = g_simple_action_new("remove-bookmark-path", G_VARIANT_TYPE_STRING);
    g_signal_connect(rm_bm_path, "activate", G_CALLBACK(on_remove_bookmark_path_action), self);
    g_action_map_add_action(G_ACTION_MAP(self), G_ACTION(rm_bm_path));
    g_object_unref(rm_bm_path);

    /* ══ Create first tab ══ */
    new_tab(self, g_get_home_dir());

    load_directory(self, g_get_home_dir());
}


GtkWindow *aether_window_new(AetherApplication *app) {
    return g_object_new(AETHER_TYPE_WINDOW, "application", app, NULL);
}



const char *aether_window_get_current_path(AetherWindow *self) {
    return self->current_path;
}


void aether_window_reload(AetherWindow *self) {
    if (!self->current_path) return;
    aether_file_repository_list_directory_async(
        self->repo, self->current_path, NULL, on_directory_loaded, self);
}


GStrv aether_window_get_selected_paths(AetherWindow *self) {
    GtkSelectionModel *sel = NULL;
    const char *visible = gtk_stack_get_visible_child_name(GTK_STACK(self->view_stack));
    if (g_strcmp0(visible, "grid") == 0) {
        sel = gtk_grid_view_get_model(GTK_GRID_VIEW(self->grid_view));
    } else if (g_strcmp0(visible, "list") == 0) {
        sel = gtk_column_view_get_model(GTK_COLUMN_VIEW(self->list_view));
    }
    if (!sel) return NULL;

    GtkBitset *bitset = gtk_selection_model_get_selection(sel);
    if (!bitset || gtk_bitset_is_empty(bitset)) {
        if (bitset) gtk_bitset_unref(bitset);
        return NULL;
    }

    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    GtkBitsetIter iter;
    guint val;
    if (gtk_bitset_iter_init_first(&iter, bitset, &val)) {
        do {
            gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), val);
            if (item) {
                if (AETHER_IS_FILE_ENTITY(item)) {
                    const char *path = aether_file_entity_get_path(AETHER_FILE_ENTITY(item));
                    if (!path) path = aether_file_entity_get_uri(AETHER_FILE_ENTITY(item));
                    if (path) g_ptr_array_add(paths, g_strdup(path));
                }
                g_object_unref(item);
            }
        } while (gtk_bitset_iter_next(&iter, &val));
    }
    gtk_bitset_unref(bitset);

    if (paths->len == 0) {
        g_ptr_array_unref(paths);
        return NULL;
    }

    g_ptr_array_add(paths, NULL);
    return (GStrv) g_ptr_array_free(paths, FALSE);
}

void aether_window_start_progress(AetherWindow *self) {
    if (self->progress_spinner) gtk_spinner_start(GTK_SPINNER(self->progress_spinner));
}

void aether_window_stop_progress(AetherWindow *self) {
    if (self->progress_spinner) gtk_spinner_stop(GTK_SPINNER(self->progress_spinner));
}


