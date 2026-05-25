#include "window_apps.h"
#include "app_uninstall_dialog.h"
#include "../../domain/app_entity.h"
#include "../../data/app_repository.h"
#include <glib/gspawn.h>

static void setup_app_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f; (void)d;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(box, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_top(box, 8);
    gtk_widget_set_margin_bottom(box, 8);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_size_request(box, 110, 110);

    GtkWidget *icon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *lbl = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(lbl), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_lines(GTK_LABEL(lbl), 2);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_END);
    gtk_label_set_justify(GTK_LABEL(lbl), GTK_JUSTIFY_CENTER);
    gtk_widget_add_css_class(lbl, "app-label");
    gtk_box_append(GTK_BOX(box), lbl);

    gtk_list_item_set_child(li, box);
}

static void bind_app_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f; (void)d;
    GtkWidget *box = gtk_list_item_get_child(li);
    GtkWidget *icon = gtk_widget_get_first_child(box);
    GtkWidget *lbl = gtk_widget_get_next_sibling(icon);

    AetherAppEntity *e = AETHER_APP_ENTITY(gtk_list_item_get_item(li));
    
    gtk_label_set_text(GTK_LABEL(lbl), aether_app_entity_get_name(e));

    const char *icon_name = aether_app_entity_get_icon_name(e);
    if (icon_name && icon_name[0] != '\0') {
        if (g_path_is_absolute(icon_name)) {
            gtk_image_set_from_file(GTK_IMAGE(icon), icon_name);
        } else {
            gtk_image_set_from_icon_name(GTK_IMAGE(icon), icon_name);
        }
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(icon), "application-x-executable");
    }
}

static void on_app_activated(GtkWidget *view, guint position, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    (void)self;
    GtkSelectionModel *sel = gtk_grid_view_get_model(GTK_GRID_VIEW(view));
    if (!sel) return;

    gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), position);
    if (!item) return;

    AetherAppEntity *e = AETHER_APP_ENTITY(item);
    const char *exec = aether_app_entity_get_exec(e);
    
    if (exec) {
        GError *err = NULL;
        if (!g_spawn_command_line_async(exec, &err)) {
            g_printerr("Failed to launch app %s: %s\n", aether_app_entity_get_name(e), err->message);
            g_error_free(err);
        }
    }
    g_object_unref(item);
}

static void on_app_action_run(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherAppEntity *e = AETHER_APP_ENTITY(user_data);
    const char *exec = aether_app_entity_get_exec(e);
    if (exec) g_spawn_command_line_async(exec, NULL);
}

static void on_app_action_shortcut(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherAppEntity *e = AETHER_APP_ENTITY(user_data);
    const char *desktop_path = aether_app_entity_get_desktop_path(e);
    if (!desktop_path) return;

    const char *home = g_get_home_dir();
    char *desktop_dir = g_build_filename(home, "Desktop", NULL);
    char *filename = g_path_get_basename(desktop_path);
    char *dest_path = g_build_filename(desktop_dir, filename, NULL);

    char *cmd = g_strdup_printf("sh -c \"cp '%s' '%s' && chmod +x '%s'\"", desktop_path, dest_path, dest_path);
    g_spawn_command_line_async(cmd, NULL);

    g_free(cmd); g_free(dest_path); g_free(filename); g_free(desktop_dir);
}

static void on_app_action_uninstall(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    (void)action; (void)parameter;
    AetherAppEntity *e = AETHER_APP_ENTITY(user_data);
    
    /* Find the window to pass as transient parent */
    GtkWidget *win = NULL;
    GListModel *toplevels = gtk_window_get_toplevels();
    if (toplevels && g_list_model_get_n_items(toplevels) > 0) {
        win = GTK_WIDGET(g_list_model_get_item(toplevels, 0));
        if (win) g_object_unref(win); /* g_list_model_get_item transfers full ref */
    }

    aether_app_uninstall_dialog_new(GTK_WINDOW(win), e);
}

static void on_app_right_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data) {
    (void)gesture; (void)n_press;
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item) return;

    AetherAppEntity *e = AETHER_APP_ENTITY(item);
    GtkWidget *box = gtk_list_item_get_child(list_item);

    if (!gtk_list_item_get_selected(list_item)) {
        GtkWidget *parent_view = gtk_widget_get_ancestor(box, GTK_TYPE_GRID_VIEW);
        if (parent_view) {
            GtkSelectionModel *sel = gtk_grid_view_get_model(GTK_GRID_VIEW(parent_view));
            if (sel) gtk_selection_model_select_item(sel, gtk_list_item_get_position(list_item), TRUE);
        }
    }

    /* Create action group for this specific item */
    GSimpleActionGroup *group = g_simple_action_group_new();
    
    GSimpleAction *act_run = g_simple_action_new("run", NULL);
    g_signal_connect(act_run, "activate", G_CALLBACK(on_app_action_run), e);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act_run));
    g_object_unref(act_run);

    GSimpleAction *act_shortcut = g_simple_action_new("shortcut", NULL);
    g_signal_connect(act_shortcut, "activate", G_CALLBACK(on_app_action_shortcut), e);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act_shortcut));
    g_object_unref(act_shortcut);

    GSimpleAction *act_uninstall = g_simple_action_new("uninstall", NULL);
    g_signal_connect(act_uninstall, "activate", G_CALLBACK(on_app_action_uninstall), e);
    g_action_map_add_action(G_ACTION_MAP(group), G_ACTION(act_uninstall));
    g_object_unref(act_uninstall);

    gtk_widget_insert_action_group(box, "app_item", G_ACTION_GROUP(group));
    g_object_unref(group);

    GMenu *menu = g_menu_new();
    g_menu_append(menu, "Run", "app_item.run");
    g_menu_append(menu, "Create Shortcut", "app_item.shortcut");
    g_menu_append(menu, "Uninstall", "app_item.uninstall");

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    gtk_widget_set_parent(popover, box);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);

    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));

    g_object_unref(menu);
}

static void on_app_item_setup_gesture(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    (void)f; (void)d;
    GtkWidget *box = gtk_list_item_get_child(li);
    GtkGesture *right_click = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(right_click), GDK_BUTTON_SECONDARY);
    g_signal_connect(right_click, "pressed", G_CALLBACK(on_app_right_clicked), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(right_click));
}

GtkWidget *setup_apps_view(AetherWindow *self) {
    /* Make sure repo is loaded */
    aether_app_repository_load_apps(self->app_repo);
    GListStore *store = aether_app_repository_get_apps(self->app_repo);

    GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));

    GtkSignalListItemFactory *factory = GTK_SIGNAL_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    g_signal_connect(factory, "setup", G_CALLBACK(setup_app_item), self);
    g_signal_connect(factory, "bind", G_CALLBACK(bind_app_item), self);

    g_signal_connect(factory, "setup", G_CALLBACK(on_app_item_setup_gesture), self);

    GtkWidget *grid = gtk_grid_view_new(GTK_SELECTION_MODEL(sel), GTK_LIST_ITEM_FACTORY(factory));
    gtk_grid_view_set_max_columns(GTK_GRID_VIEW(grid), 20);
    gtk_grid_view_set_min_columns(GTK_GRID_VIEW(grid), 1);
    gtk_widget_set_margin_start(grid, 12);
    gtk_widget_set_margin_end(grid, 12);
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_add_css_class(grid, "apps-grid");

    g_signal_connect(grid, "activate", G_CALLBACK(on_app_activated), self);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), grid);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

    return scrolled;
}

void show_apps_view(AetherWindow *self) {
    if (self->apps_view) {
        gtk_stack_set_visible_child(GTK_STACK(self->view_stack), self->apps_view);
        /* Hide sort buttons and pathbar since they don't apply to apps */
        gtk_widget_set_visible(self->path_bar, FALSE);
        gtk_widget_set_visible(self->sort_btn, FALSE);
        gtk_widget_set_visible(self->btn_grid, FALSE);
        gtk_widget_set_visible(self->btn_list, FALSE);
    }
}
