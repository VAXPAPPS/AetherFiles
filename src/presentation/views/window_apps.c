#include "window_apps.h"
#include "../../domain/app_entity.h"
#include "../../data/app_repository.h"

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
    GtkSelectionModel *sel = gtk_grid_view_get_model(GTK_GRID_VIEW(view));
    if (!sel) return;

    gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), position);
    if (!item) return;

    AetherAppEntity *e = AETHER_APP_ENTITY(item);
    const char *exec = aether_app_entity_get_exec(e);
    
    if (exec) {
        GError *err = NULL;
        /* Simple async launch. We use g_spawn_command_line_async which parses the command line easily */
        if (!g_spawn_command_line_async(exec, &err)) {
            g_printerr("Failed to launch app %s: %s\n", aether_app_entity_get_name(e), err->message);
            g_error_free(err);
        }
    }
    
    g_object_unref(item);
}

GtkWidget *setup_apps_view(AetherWindow *self) {
    /* Make sure repo is loaded */
    aether_app_repository_load_apps(self->app_repo);
    GListStore *store = aether_app_repository_get_apps(self->app_repo);

    GtkSingleSelection *sel = gtk_single_selection_new(G_LIST_MODEL(store));

    GtkSignalListItemFactory *factory = GTK_SIGNAL_LIST_ITEM_FACTORY(gtk_signal_list_item_factory_new());
    g_signal_connect(factory, "setup", G_CALLBACK(setup_app_item), self);
    g_signal_connect(factory, "bind", G_CALLBACK(bind_app_item), self);

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
