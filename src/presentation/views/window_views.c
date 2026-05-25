#include "window_private.h"
#include <glib/gi18n.h>

void setup_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
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

void bind_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
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

void unbind_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *icon  = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(icon);
    gtk_image_clear(GTK_IMAGE(icon));
    gtk_label_set_text(GTK_LABEL(label), "");
}

void setup_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
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

void bind_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
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

void unbind_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *icon  = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(icon);
    gtk_image_clear(GTK_IMAGE(icon));
    gtk_label_set_text(GTK_LABEL(label), "");
}

void setup_size_col_item(GtkListItemFactory *f, GtkListItem *item, gpointer ud) {
    (void)f; (void)ud;
    GtkWidget *lbl = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(lbl), 1.0f);
    gtk_widget_add_css_class(lbl, "dim-label");
    gtk_widget_set_margin_end(lbl, 8);
    gtk_list_item_set_child(item, lbl);
}

void bind_size_col_item(GtkListItemFactory *f, GtkListItem *item, gpointer ud) {
    (void)f; (void)ud;
    AetherFileEntity *e = AETHER_FILE_ENTITY(gtk_list_item_get_item(item));
    if (!e) return;
    GtkWidget *lbl = gtk_list_item_get_child(item);
    if (aether_file_entity_is_directory(e)) {
        gtk_label_set_text(GTK_LABEL(lbl), "—");
    } else {
        goffset sz = aether_file_entity_get_size(e);
        char *s;
        if (sz < 1024)            s = g_strdup_printf("%" G_GOFFSET_FORMAT " B", sz);
        else if (sz < 1024*1024)  s = g_strdup_printf("%.0f KB", sz/1024.0);
        else if (sz < 1024*1024*1024LL) s = g_strdup_printf("%.1f MB", sz/(1024.0*1024.0));
        else                      s = g_strdup_printf("%.2f GB", sz/(1024.0*1024.0*1024.0));
        gtk_label_set_text(GTK_LABEL(lbl), s);
        g_free(s);
    }
}

void unbind_size_col_item(GtkListItemFactory *f, GtkListItem *item, gpointer ud) {
    (void)f; (void)ud;
    gtk_label_set_text(GTK_LABEL(gtk_list_item_get_child(item)), "");
}

gint compare_entities(gconstpointer a, gconstpointer b, gpointer user_data) {
    AetherWindow     *self = AETHER_WINDOW(user_data);
    AetherFileEntity *f1   = AETHER_FILE_ENTITY(a);
    AetherFileEntity *f2   = AETHER_FILE_ENTITY(b);

    /* Dirs always first */
    gboolean d1 = aether_file_entity_is_directory(f1);
    gboolean d2 = aether_file_entity_is_directory(f2);
    if (d1 && !d2) return GTK_ORDERING_SMALLER;
    if (!d1 && d2) return GTK_ORDERING_LARGER;

    int cmp = 0;
    switch (self ? self->sort_mode : 0) {
    case 1: /* Size */
        {
            goffset s1 = aether_file_entity_get_size(f1);
            goffset s2 = aether_file_entity_get_size(f2);
            cmp = (s1 < s2) ? -1 : (s1 > s2) ? 1 : 0;
        }
        break;
    case 2: /* Type / extension */
        {
            const char *n1 = aether_file_entity_get_name(f1) ?: "";
            const char *n2 = aether_file_entity_get_name(f2) ?: "";
            const char *e1 = strrchr(n1, '.');
            const char *e2 = strrchr(n2, '.');
            char *t1 = g_utf8_casefold(e1 ? e1 : "", -1);
            char *t2 = g_utf8_casefold(e2 ? e2 : "", -1);
            cmp = g_utf8_collate(t1, t2);
            g_free(t1); g_free(t2);
        }
        break;
    case 0: /* Name (default) */
    default:
        {
            char *k1 = g_utf8_casefold(aether_file_entity_get_name(f1) ?: "", -1);
            char *k2 = g_utf8_casefold(aether_file_entity_get_name(f2) ?: "", -1);
            cmp = g_utf8_collate(k1, k2);
            g_free(k1); g_free(k2);
        }
        break;
    }

    if (self && !self->sort_asc) cmp = -cmp;
    if (cmp < 0) return GTK_ORDERING_SMALLER;
    if (cmp > 0) return GTK_ORDERING_LARGER;
    return GTK_ORDERING_EQUAL;
}

gboolean name_filter_func(gpointer item, gpointer user_data) {
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

void on_item_right_clicked(GtkGestureClick *gesture, int n_press,
                                   double x, double y, gpointer user_data)
{
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item) return;

    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    GtkWidget *box = gtk_list_item_get_child(list_item);
    const char *path = aether_file_entity_get_path(entity);
    GVariant   *pv   = g_variant_new_string(path);

    if (!gtk_list_item_get_selected(list_item)) {
        GtkWidget *parent_view = gtk_widget_get_ancestor(box, GTK_TYPE_GRID_VIEW);
        if (!parent_view)
            parent_view = gtk_widget_get_ancestor(box, GTK_TYPE_COLUMN_VIEW);
        if (parent_view) {
            GtkSelectionModel *sel = NULL;
            if (GTK_IS_GRID_VIEW(parent_view))
                sel = gtk_grid_view_get_model(GTK_GRID_VIEW(parent_view));
            else if (GTK_IS_COLUMN_VIEW(parent_view))
                sel = gtk_column_view_get_model(GTK_COLUMN_VIEW(parent_view));
            if (sel) {
                guint pos = gtk_list_item_get_position(list_item);
                gtk_selection_model_select_item(sel, pos, TRUE);
            }
        }
    }

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
    g_menu_item_set_action_and_target_value(mi, "app.cut",  NULL);
    g_menu_append_item(s2, mi); g_object_unref(mi);
    mi = g_menu_item_new("Copy", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.copy", NULL);
    g_menu_append_item(s2, mi); g_object_unref(mi);
    mi = g_menu_item_new("Paste", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.paste", NULL);
    g_menu_append_item(s2, mi); g_object_unref(mi);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s2)); g_object_unref(s2);

    /* Manage */
    GMenu *s3 = g_menu_new();
    mi = g_menu_item_new("Rename…", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.rename-path", pv);
    g_menu_append_item(s3, mi); g_object_unref(mi);

    if (!aether_file_entity_is_directory(entity)) {
        mi = g_menu_item_new("Set as Background…", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.set_background", pv);
        g_menu_append_item(s3, mi); g_object_unref(mi);
    }

    mi = g_menu_item_new("Move to Trash", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.trash", NULL);
    g_menu_append_item(s3, mi); g_object_unref(mi);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s3)); g_object_unref(s3);

    /* Properties + Bookmarks */
    GMenu *s4 = g_menu_new();
    if (aether_file_entity_is_directory(entity)) {
        mi = g_menu_item_new("Add to Bookmarks", NULL);
        g_menu_item_set_action_and_target_value(mi, "win.add-bookmark-path", pv);
        g_menu_append_item(s4, mi); g_object_unref(mi);
    }
    mi = g_menu_item_new("Properties", NULL);
    g_menu_item_set_action_and_target_value(mi, "app.properties", pv);
    g_menu_append_item(s4, mi); g_object_unref(mi);
    g_menu_append_section(menu, NULL, G_MENU_MODEL(s4)); g_object_unref(s4);

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(box));

    if (root) {
        gtk_widget_set_parent(popover, root);
        g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
        double xv = 0, yv = 0;
        gtk_widget_translate_coordinates(box, root, x, y, &xv, &yv);
        GdkRectangle rect = { (int)xv, (int)yv, 1, 1 };
        gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
        gtk_popover_popup(GTK_POPOVER(popover));
    } else {
        g_object_unref(popover);
    }
    g_object_unref(menu);
}

void on_popover_closed(GtkPopover *popover, gpointer user_data) {
    gtk_widget_unparent(GTK_WIDGET(popover));
}

