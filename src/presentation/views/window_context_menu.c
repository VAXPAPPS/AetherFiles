#include "window_private.h"
#include <glib/gi18n.h>

static GVariant* safe_variant_string(const char *str) {
    if (!str) return g_variant_new_string("");
    if (g_utf8_validate(str, -1, NULL)) return g_variant_new_string(str);
    char *utf8 = g_filename_to_utf8(str, -1, NULL, NULL, NULL);
    if (utf8) {
        GVariant *v = g_variant_new_take_string(utf8);
        return v;
    }
    return g_variant_new_string("");
}

static gboolean idle_unparent(gpointer data) {
    gtk_widget_unparent(GTK_WIDGET(data));
    return G_SOURCE_REMOVE;
}

void on_popover_closed(GtkPopover *popover, gpointer user_data) {
    (void)user_data;
    g_idle_add(idle_unparent, popover);
}

static gboolean is_media_file(const char *name) {
    if (!name) return FALSE;
    const char *ext = strrchr(name, '.');
    if (!ext) return FALSE;
    ext++;

    const char *media_exts[] = {
        "png", "jpg", "jpeg", "gif", "bmp", "webp", "svg",
        "mp4", "mkv", "webm", "avi", "mov", "flv", "wmv",
        NULL
    };

    char *lower_ext = g_utf8_casefold(ext, -1);
    gboolean is_media = FALSE;
    for (int i = 0; media_exts[i] != NULL; i++) {
        if (g_strcmp0(lower_ext, media_exts[i]) == 0) {
            is_media = TRUE;
            break;
        }
    }
    g_free(lower_ext);
    return is_media;
}

static gboolean is_archive_file(const char *name) {
    if (!name) return FALSE;
    char *lower_name = g_utf8_casefold(name, -1);
    gboolean is_archive = g_str_has_suffix(lower_name, ".zip") || 
                          g_str_has_suffix(lower_name, ".tar.xz") || 
                          g_str_has_suffix(lower_name, ".7z") ||
                          g_str_has_suffix(lower_name, ".tar") ||
                          g_str_has_suffix(lower_name, ".tar.gz");
    g_free(lower_name);
    return is_archive;
}

void on_item_right_clicked(GtkGestureClick *gesture, int n_press,
                                   double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press;
    GtkListItem *list_item = GTK_LIST_ITEM(user_data);
    gpointer item = gtk_list_item_get_item(list_item);
    if (!item) return;

    AetherFileEntity *entity = AETHER_FILE_ENTITY(item);
    GtkWidget *box = gtk_list_item_get_child(list_item);
    const char *path = aether_file_entity_get_path(entity);
    if (!path) path = aether_file_entity_get_uri(entity);

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
    GtkWidget *root = GTK_WIDGET(gtk_widget_get_root(box));
    AetherWindow *self = AETHER_IS_WINDOW(root) ? AETHER_WINDOW(root) : NULL;
    const char *current_path = self ? aether_window_get_current_path(self) : NULL;
    gboolean in_trash = current_path && g_str_has_prefix(current_path, "trash:");

    if (in_trash) {
        GMenu *s1 = g_menu_new();
        GMenuItem *mi = g_menu_item_new("Restore", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.restore", NULL);
        g_menu_append_item(s1, mi); g_object_unref(mi);
        
        mi = g_menu_item_new("Delete Permanently", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.delete-permanently", NULL);
        g_menu_append_item(s1, mi); g_object_unref(mi);
        g_menu_append_section(menu, NULL, G_MENU_MODEL(s1)); g_object_unref(s1);
        
        GMenu *s2 = g_menu_new();
        mi = g_menu_item_new("Properties", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.properties", safe_variant_string(path ? path : ""));
        g_menu_append_item(s2, mi); g_object_unref(mi);
        g_menu_append_section(menu, NULL, G_MENU_MODEL(s2)); g_object_unref(s2);
    } else {
        /* Open */
        GMenu *s1 = g_menu_new();
        GMenuItem *mi = g_menu_item_new("Open", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.open", safe_variant_string(path ? path : ""));
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
        mi = g_menu_item_new("Share via Bluetooth", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.share-bluetooth", NULL);
        g_menu_append_item(s3, mi); g_object_unref(mi);

        mi = g_menu_item_new("Rename…", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.rename-path", safe_variant_string(path ? path : ""));
        g_menu_append_item(s3, mi); g_object_unref(mi);

        if (is_media_file(aether_file_entity_get_name(entity))) {
            mi = g_menu_item_new("Set as Background…", NULL);
            g_menu_item_set_action_and_target_value(mi, "app.set_background", safe_variant_string(path ? path : ""));
            g_menu_append_item(s3, mi); g_object_unref(mi);
        }

        if (is_archive_file(aether_file_entity_get_name(entity))) {
            mi = g_menu_item_new("Extract Here", NULL);
            g_menu_item_set_action_and_target_value(mi, "app.extract", safe_variant_string(path ? path : ""));
            g_menu_append_item(s3, mi); g_object_unref(mi);
        }
        
        GMenu *compress_menu = g_menu_new();
        mi = g_menu_item_new(".zip", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.compress", g_variant_new_string("zip"));
        g_menu_append_item(compress_menu, mi); g_object_unref(mi);
        
        mi = g_menu_item_new(".tar.xz", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.compress", g_variant_new_string("tar.xz"));
        g_menu_append_item(compress_menu, mi); g_object_unref(mi);
        
        mi = g_menu_item_new(".7z", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.compress", g_variant_new_string("7z"));
        g_menu_append_item(compress_menu, mi); g_object_unref(mi);
        
        mi = g_menu_item_new("Compress…", NULL);
        g_menu_item_set_submenu(mi, G_MENU_MODEL(compress_menu));
        g_menu_append_item(s3, mi); g_object_unref(mi);
        g_object_unref(compress_menu);

        mi = g_menu_item_new("Move to Trash", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.trash", NULL);
        g_menu_append_item(s3, mi); g_object_unref(mi);
        g_menu_append_section(menu, NULL, G_MENU_MODEL(s3)); g_object_unref(s3);

        /* Properties + Bookmarks */
        GMenu *s4 = g_menu_new();
        if (aether_file_entity_is_directory(entity)) {
            if (is_bookmarked(path)) {
                mi = g_menu_item_new("Remove from Bookmarks", NULL);
                g_menu_item_set_action_and_target_value(mi, "win.remove-bookmark-path", safe_variant_string(path ? path : ""));
            } else {
                mi = g_menu_item_new("Add to Bookmarks", NULL);
                g_menu_item_set_action_and_target_value(mi, "win.add-bookmark-path", safe_variant_string(path ? path : ""));
            }
            g_menu_append_item(s4, mi); g_object_unref(mi);
        }
        mi = g_menu_item_new("Properties", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.properties", safe_variant_string(path ? path : ""));
        g_menu_append_item(s4, mi); g_object_unref(mi);
        g_menu_append_section(menu, NULL, G_MENU_MODEL(s4)); g_object_unref(s4);
    }

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));

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

void on_background_right_clicked(GtkGestureClick *gesture, int n_press,
                                 double x, double y, gpointer user_data)
{
    (void)gesture; (void)n_press;
    AetherWindow *self = AETHER_WINDOW(user_data);
    
    const char *current_path = aether_window_get_current_path(self);
    if (!current_path) return;

    gboolean in_trash = g_str_has_prefix(current_path, "trash:");

    GMenu *menu = g_menu_new();

    if (in_trash) {
        GMenuItem *mi = g_menu_item_new("Restore All", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.restore-all", NULL);
        g_menu_append_item(menu, mi); g_object_unref(mi);

        mi = g_menu_item_new("Empty Trash", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.empty-trash", NULL);
        g_menu_append_item(menu, mi); g_object_unref(mi);
    } else {
        GMenuItem *mi;

        /* Create Document */
        mi = g_menu_item_new("Create Document", NULL);
        g_menu_item_set_action_and_target_value(mi, "win.create-document", NULL);
        g_menu_append_item(menu, mi); g_object_unref(mi);

        /* Create Folder */
        mi = g_menu_item_new("Create Folder", NULL);
        g_menu_item_set_action_and_target_value(mi, "win.create-folder", NULL);
        g_menu_append_item(menu, mi); g_object_unref(mi);

        /* Select All */
        mi = g_menu_item_new("Select All", NULL);
        g_menu_item_set_action_and_target_value(mi, "win.select-all", NULL);
        g_menu_append_item(menu, mi); g_object_unref(mi);

        /* Open Terminal */
        mi = g_menu_item_new("Open Terminal", NULL);
        g_menu_item_set_action_and_target_value(mi, "win.open-terminal", NULL);
        g_menu_append_item(menu, mi); g_object_unref(mi);

        /* Paste */
        mi = g_menu_item_new("Paste", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.paste", NULL);
        g_menu_append_item(menu, mi); g_object_unref(mi);

        /* Properties */
        mi = g_menu_item_new("Properties", NULL);
        g_menu_item_set_action_and_target_value(mi, "app.properties", safe_variant_string(current_path));
        g_menu_append_item(menu, mi); g_object_unref(mi);
    }

    GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(menu));
    GtkWidget *widget = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(gesture));
    
    gtk_widget_set_parent(popover, widget);
    g_signal_connect(popover, "closed", G_CALLBACK(on_popover_closed), NULL);
    
    GdkRectangle rect = { (int)x, (int)y, 1, 1 };
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &rect);
    gtk_popover_popup(GTK_POPOVER(popover));
    
    g_object_unref(menu);
}
