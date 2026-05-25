#include "window_private.h"
#include <glib/gi18n.h>

#define BOOKMARKS_FILE ".config/aetherfiles-bookmarks"

GtkWidget *make_sidebar_row(const char *name, const char *icon_name);

void load_bookmarks(AetherWindow *self) {
    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *contents = NULL;
    if (!g_file_get_contents(bm_path, &contents, NULL, NULL)) {
        g_free(bm_path);
        return;
    }
    g_free(bm_path);

    /* Remove old bookmark rows */
    GtkListBoxRow *row;
    int count = 0;
    while ((row = gtk_list_box_get_row_at_index(
                GTK_LIST_BOX(self->sidebar_list),
                self->bookmark_row_start + count)) != NULL) {
        gtk_list_box_remove(GTK_LIST_BOX(self->sidebar_list), GTK_WIDGET(row));
    }

    char **lines = g_strsplit(contents, "\n", -1);
    g_free(contents);

    for (int i = 0; lines[i] != NULL; i++) {
        const char *line = lines[i];
        if (!line || line[0] == '\0') continue;
        /* Each line: file:///path [label] */
        char **parts = g_strsplit(line, " ", 2);
        const char *uri = parts[0];
        GFile *f = g_file_new_for_uri(uri);
        char  *path = g_file_get_path(f);
        g_object_unref(f);

        if (!path) { g_strfreev(parts); continue; }

        const char *label = (parts[1] && parts[1][0]) ?
                             parts[1] : g_path_get_basename(path);

        GtkWidget *bm_row = make_sidebar_row(label, "bookmark-new-symbolic");
        g_object_set_data_full(G_OBJECT(bm_row), "path", g_strdup(path), g_free);
        gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), bm_row);

        g_free(path);
        g_strfreev(parts);
    }
    g_strfreev(lines);
}

void save_bookmark(const char *path) {
    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *existing = NULL;
    g_file_get_contents(bm_path, &existing, NULL, NULL);

    char *uri     = g_filename_to_uri(path, NULL, NULL);
    char *new_line = g_strdup_printf("%s\n", uri);
    char *new_contents;
    if (existing)
        new_contents = g_strconcat(existing, new_line, NULL);
    else
        new_contents = g_strdup(new_line);

    g_file_set_contents(bm_path, new_contents, -1, NULL);
    g_free(existing); g_free(uri); g_free(new_line);
    g_free(new_contents); g_free(bm_path);
}

GtkWidget *make_sidebar_row(const char *name, const char *icon_name) {
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

void add_sidebar_separator(AetherWindow *self) {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_widget_set_margin_top(sep, 4);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), sep);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

void add_sidebar_header(AetherWindow *self, const char *title) {
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

void add_sidebar_place(AetherWindow *self, const char *name,
                               const char *icon, const char *path)
{
    GtkWidget *row = make_sidebar_row(name, icon);
    g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path), g_free);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

void setup_sidebar(AetherWindow *self) {
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

void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(row), "path");
    if (path) load_directory(self, path);
}

