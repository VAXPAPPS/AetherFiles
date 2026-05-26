#include "window_private.h"
#include <glib/gi18n.h>
#include "../../data/thumbnail_manager.h"

static void on_thumbnail_updated(AetherFileEntity *e, gpointer user_data) {
    GtkStack *stack = GTK_STACK(user_data);
    GtkWidget *picture = gtk_stack_get_child_by_name(stack, "thumbnail");
    GdkTexture *tex = aether_file_entity_get_thumbnail(e);
    if (tex && picture) {
        gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(tex));
        gtk_stack_set_visible_child_name(stack, "thumbnail");
    }
}

static void on_thumbnail_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    AetherThumbnailManager *tm = AETHER_THUMBNAIL_MANAGER(source);
    AetherFileEntity *e = AETHER_FILE_ENTITY(user_data);
    
    GError *err = NULL;
    GdkTexture *texture = aether_thumbnail_manager_get_thumbnail_finish(tm, res, &err);
    if (texture) {
        aether_file_entity_set_thumbnail(e, texture);
        g_object_unref(texture);
    } else {
        if (err) g_error_free(err);
    }
    
    aether_file_entity_set_is_loading_thumbnail(e, FALSE);
    g_object_unref(e);
}

void setup_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(box, "nautilus-grid-cell");
    gtk_widget_set_size_request(box, 110, 110);
    gtk_widget_set_margin_top(box, 6);
    gtk_widget_set_margin_bottom(box, 6);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    GtkWidget *stack = gtk_stack_new();
    gtk_widget_set_halign(stack, GTK_ALIGN_CENTER);

    GtkWidget *icon = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 64);
    gtk_stack_add_named(GTK_STACK(stack), icon, "icon");
    
    GtkWidget *picture = gtk_picture_new();
    gtk_widget_set_size_request(picture, 64, 64);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_FILL);
    gtk_stack_add_named(GTK_STACK(stack), picture, "thumbnail");

    GtkWidget *label = gtk_label_new("");
    gtk_label_set_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_wrap_mode(GTK_LABEL(label), PANGO_WRAP_WORD_CHAR);
    gtk_label_set_max_width_chars(GTK_LABEL(label), 12);
    gtk_label_set_lines(GTK_LABEL(label), 2);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_CENTER);
    gtk_widget_set_halign(label, GTK_ALIGN_CENTER);

    gtk_box_append(GTK_BOX(box), stack);
    gtk_box_append(GTK_BOX(box), label);

    GtkGesture *g = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), GDK_BUTTON_SECONDARY);
    g_signal_connect(g, "pressed", G_CALLBACK(on_item_right_clicked), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(g));

    GtkDragSource *source = gtk_drag_source_new();
    gtk_drag_source_set_actions(source, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(source, "prepare", G_CALLBACK(on_drag_prepare), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(source));

    GtkDropTarget *item_drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(item_drop, "accept", G_CALLBACK(on_item_drop_accept), li);
    g_signal_connect(item_drop, "drop", G_CALLBACK(on_item_drop), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(item_drop));

    gtk_list_item_set_child(li, box);
}

void bind_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *stack = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(stack);
    GtkWidget *icon = gtk_stack_get_child_by_name(GTK_STACK(stack), "icon");
    GtkWidget *picture = gtk_stack_get_child_by_name(GTK_STACK(stack), "thumbnail");
    gpointer   item  = gtk_list_item_get_item(li);
    if (!item || !AETHER_IS_FILE_ENTITY(item)) return;

    AetherFileEntity *e = AETHER_FILE_ENTITY(item);
    
    gulong sig_id = g_signal_connect(e, "thumbnail-updated", G_CALLBACK(on_thumbnail_updated), stack);
    g_object_set_data(G_OBJECT(stack), "thumb-sig-id", GUINT_TO_POINTER(sig_id));
    g_object_set_data(G_OBJECT(stack), "thumb-entity", e);
    
    GdkTexture *thumbnail = aether_file_entity_get_thumbnail(e);
    if (thumbnail) {
        gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(thumbnail));
        gtk_stack_set_visible_child_name(GTK_STACK(stack), "thumbnail");
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(icon), aether_file_entity_get_icon_name(e));
        gtk_stack_set_visible_child_name(GTK_STACK(stack), "icon");
        
        if (!aether_file_entity_get_is_loading_thumbnail(e)) {
            const char *icon_name = aether_file_entity_get_icon_name(e);
            if (g_str_has_prefix(icon_name, "image") || g_str_has_prefix(icon_name, "video")) {
                aether_file_entity_set_is_loading_thumbnail(e, TRUE);
                char *mime = g_strdup_printf("%s/generic", g_str_has_prefix(icon_name, "image") ? "image" : "video");
                aether_thumbnail_manager_get_thumbnail_async(
                    aether_thumbnail_manager_get_default(), 
                    aether_file_entity_get_uri(e), 
                    mime, NULL, on_thumbnail_ready, g_object_ref(e)
                );
                g_free(mime);
            }
        }
    }
    
    gtk_label_set_text(GTK_LABEL(label), aether_file_entity_get_name(e));
}

void unbind_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *stack = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(stack);
    GtkWidget *icon = gtk_stack_get_child_by_name(GTK_STACK(stack), "icon");
    GtkWidget *picture = gtk_stack_get_child_by_name(GTK_STACK(stack), "thumbnail");
    
    AetherFileEntity *e = g_object_get_data(G_OBJECT(stack), "thumb-entity");
    gulong sig_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(stack), "thumb-sig-id"));
    if (e && sig_id > 0) {
        g_signal_handler_disconnect(e, sig_id);
    }
    g_object_set_data(G_OBJECT(stack), "thumb-sig-id", NULL);
    g_object_set_data(G_OBJECT(stack), "thumb-entity", NULL);
    
    gtk_image_clear(GTK_IMAGE(icon));
    gtk_picture_set_paintable(GTK_PICTURE(picture), NULL);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "icon");
    gtk_label_set_text(GTK_LABEL(label), "");
}

void setup_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget *stack = gtk_stack_new();
    GtkWidget *icon  = gtk_image_new();
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 20);
    gtk_stack_add_named(GTK_STACK(stack), icon, "icon");
    
    GtkWidget *picture = gtk_picture_new();
    gtk_widget_set_size_request(picture, 20, 20);
    gtk_picture_set_can_shrink(GTK_PICTURE(picture), TRUE);
    gtk_picture_set_content_fit(GTK_PICTURE(picture), GTK_CONTENT_FIT_FILL);
    gtk_stack_add_named(GTK_STACK(stack), picture, "thumbnail");
    GtkWidget *label = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), stack);
    gtk_box_append(GTK_BOX(box), label);

    GtkGesture *g = gtk_gesture_click_new();
    gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(g), GDK_BUTTON_SECONDARY);
    g_signal_connect(g, "pressed", G_CALLBACK(on_item_right_clicked), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(g));

    GtkDragSource *source = gtk_drag_source_new();
    gtk_drag_source_set_actions(source, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(source, "prepare", G_CALLBACK(on_drag_prepare), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(source));

    GtkDropTarget *item_drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(item_drop, "accept", G_CALLBACK(on_item_drop_accept), li);
    g_signal_connect(item_drop, "drop", G_CALLBACK(on_item_drop), li);
    gtk_widget_add_controller(box, GTK_EVENT_CONTROLLER(item_drop));

    gtk_list_item_set_child(li, box);
}

void bind_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *stack = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(stack);
    GtkWidget *icon = gtk_stack_get_child_by_name(GTK_STACK(stack), "icon");
    GtkWidget *picture = gtk_stack_get_child_by_name(GTK_STACK(stack), "thumbnail");
    gpointer   item  = gtk_list_item_get_item(li);
    if (!item || !AETHER_IS_FILE_ENTITY(item)) return;

    AetherFileEntity *e = AETHER_FILE_ENTITY(item);
    
    gulong sig_id = g_signal_connect(e, "thumbnail-updated", G_CALLBACK(on_thumbnail_updated), stack);
    g_object_set_data(G_OBJECT(stack), "thumb-sig-id", GUINT_TO_POINTER(sig_id));
    g_object_set_data(G_OBJECT(stack), "thumb-entity", e);
    
    GdkTexture *thumbnail = aether_file_entity_get_thumbnail(e);
    if (thumbnail) {
        gtk_picture_set_paintable(GTK_PICTURE(picture), GDK_PAINTABLE(thumbnail));
        gtk_stack_set_visible_child_name(GTK_STACK(stack), "thumbnail");
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(icon), aether_file_entity_get_icon_name(e));
        gtk_stack_set_visible_child_name(GTK_STACK(stack), "icon");
        
        if (!aether_file_entity_get_is_loading_thumbnail(e)) {
            const char *icon_name = aether_file_entity_get_icon_name(e);
            if (g_str_has_prefix(icon_name, "image") || g_str_has_prefix(icon_name, "video")) {
                aether_file_entity_set_is_loading_thumbnail(e, TRUE);
                char *mime = g_strdup_printf("%s/generic", g_str_has_prefix(icon_name, "image") ? "image" : "video");
                aether_thumbnail_manager_get_thumbnail_async(
                    aether_thumbnail_manager_get_default(), 
                    aether_file_entity_get_uri(e), 
                    mime, NULL, on_thumbnail_ready, g_object_ref(e)
                );
                g_free(mime);
            }
        }
    }
    
    gtk_label_set_text(GTK_LABEL(label), aether_file_entity_get_name(e));
}

void unbind_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d) {
    GtkWidget *box   = gtk_list_item_get_child(li);
    if (!box) return;
    GtkWidget *stack = gtk_widget_get_first_child(box);
    GtkWidget *label = gtk_widget_get_next_sibling(stack);
    GtkWidget *icon = gtk_stack_get_child_by_name(GTK_STACK(stack), "icon");
    GtkWidget *picture = gtk_stack_get_child_by_name(GTK_STACK(stack), "thumbnail");
    
    AetherFileEntity *e = g_object_get_data(G_OBJECT(stack), "thumb-entity");
    gulong sig_id = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(stack), "thumb-sig-id"));
    if (e && sig_id > 0) {
        g_signal_handler_disconnect(e, sig_id);
    }
    g_object_set_data(G_OBJECT(stack), "thumb-sig-id", NULL);
    g_object_set_data(G_OBJECT(stack), "thumb-entity", NULL);
    
    gtk_image_clear(GTK_IMAGE(icon));
    gtk_picture_set_paintable(GTK_PICTURE(picture), NULL);
    gtk_stack_set_visible_child_name(GTK_STACK(stack), "icon");
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

    /* Normal before hidden */
    const char *n1 = aether_file_entity_get_name(f1);
    const char *n2 = aether_file_entity_get_name(f2);
    gboolean h1 = n1 && n1[0] == '.';
    gboolean h2 = n2 && n2[0] == '.';
    if (!h1 && h2) return GTK_ORDERING_SMALLER;
    if (h1 && !h2) return GTK_ORDERING_LARGER;

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
    AetherFileEntity *e = AETHER_FILE_ENTITY(item);
    const char *name = aether_file_entity_get_name(e);
    if (!name) return FALSE;
    
    /* Filter hidden files */
    if (!self->show_hidden && name[0] == '.') {
        return FALSE;
    }
    
    if (!self->filter_string || self->filter_string[0] == '\0') return TRUE;

    char *name_lower   = g_utf8_casefold(name, -1);
    char *filter_lower = g_utf8_casefold(self->filter_string, -1);
    gboolean match = strstr(name_lower, filter_lower) != NULL;
    g_free(name_lower);
    g_free(filter_lower);
    return match;
}

