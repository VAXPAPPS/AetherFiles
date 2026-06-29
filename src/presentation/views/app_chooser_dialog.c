#include "app_chooser_dialog.h"
#include <string.h>

struct _AetherAppChooserDialog {
    GtkWindow parent_instance;
    GFile *file;
    GtkWidget *list_box;
    GtkWidget *search_entry;
};

G_DEFINE_TYPE(AetherAppChooserDialog, aether_app_chooser_dialog, GTK_TYPE_WINDOW)

static void aether_app_chooser_dialog_finalize(GObject *object) {
    AetherAppChooserDialog *self = AETHER_APP_CHOOSER_DIALOG(object);
    if (self->file) g_object_unref(self->file);
    G_OBJECT_CLASS(aether_app_chooser_dialog_parent_class)->finalize(object);
}

static void on_app_selected(GtkListBox *box, GtkListBoxRow *row, AetherAppChooserDialog *self) {
    if (!row) return;

    GAppInfo *app_info = g_object_get_data(G_OBJECT(row), "app-info");
    if (!app_info) return;

    if (self->file) {
        GList *uris = g_list_append(NULL, g_file_get_uri(self->file));
        GError *err = NULL;
        if (!g_app_info_launch_uris(app_info, uris, NULL, &err)) {
            g_printerr("Launch error: %s\n", err->message);
            g_error_free(err);
        }
        g_list_free_full(uris, g_free);
    }
    
    gtk_window_destroy(GTK_WINDOW(self));
}

static GtkWidget *create_app_row(GAppInfo *app_info) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);

    GIcon *icon = g_app_info_get_icon(app_info);
    GtkWidget *image;
    if (icon) {
        image = gtk_image_new_from_gicon(icon);
    } else {
        image = gtk_image_new_from_icon_name("application-x-executable");
    }
    gtk_image_set_pixel_size(GTK_IMAGE(image), 32);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *name_label = gtk_label_new(g_app_info_get_display_name(app_info));
    gtk_widget_set_halign(name_label, GTK_ALIGN_START);
    
    const char *desc = g_app_info_get_description(app_info);
    GtkWidget *desc_label = gtk_label_new(desc ? desc : g_app_info_get_executable(app_info));
    gtk_widget_add_css_class(desc_label, "dim-label");
    gtk_widget_set_halign(desc_label, GTK_ALIGN_START);
    gtk_label_set_ellipsize(GTK_LABEL(desc_label), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(desc_label), 40);

    gtk_box_append(GTK_BOX(vbox), name_label);
    gtk_box_append(GTK_BOX(vbox), desc_label);

    gtk_box_append(GTK_BOX(box), image);
    gtk_box_append(GTK_BOX(box), vbox);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data_full(G_OBJECT(row), "app-info", g_object_ref(app_info), g_object_unref);

    return row;
}

static gint sort_apps(GtkListBoxRow *row1, GtkListBoxRow *row2, gpointer user_data) {
    gboolean is_rec1 = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row1), "is-recommended"));
    gboolean is_rec2 = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row2), "is-recommended"));
    
    if (is_rec1 != is_rec2) {
        return is_rec1 ? -1 : 1;
    }

    GAppInfo *app1 = g_object_get_data(G_OBJECT(row1), "app-info");
    GAppInfo *app2 = g_object_get_data(G_OBJECT(row2), "app-info");
    if (!app1 || !app2) return 0;
    
    /* sort by display name */
    const char *name1 = g_app_info_get_display_name(app1);
    const char *name2 = g_app_info_get_display_name(app2);
    if (!name1) name1 = "";
    if (!name2) name2 = "";
    return g_utf8_collate(name1, name2);
}

static gboolean filter_apps(GtkListBoxRow *row, gpointer user_data) {
    AetherAppChooserDialog *self = AETHER_APP_CHOOSER_DIALOG(user_data);
    const char *search_text = gtk_editable_get_text(GTK_EDITABLE(self->search_entry));
    if (!search_text || search_text[0] == '\0') return TRUE;

    GAppInfo *app = g_object_get_data(G_OBJECT(row), "app-info");
    if (!app) return FALSE;

    const char *name = g_app_info_get_display_name(app);
    if (!name) return FALSE;

    char *lower_name = g_utf8_strdown(name, -1);
    char *lower_search = g_utf8_strdown(search_text, -1);
    
    gboolean match = strstr(lower_name, lower_search) != NULL;
    
    g_free(lower_name);
    g_free(lower_search);
    return match;
}

static void header_func(GtkListBoxRow *row, GtkListBoxRow *before, gpointer user_data) {
    gboolean is_rec = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "is-recommended"));
    
    if (!before) {
        GtkWidget *header = gtk_label_new(is_rec ? "Recommended Applications" : "All Applications");
        gtk_widget_add_css_class(header, "dim-label");
        gtk_widget_set_halign(header, GTK_ALIGN_START);
        gtk_widget_set_margin_top(header, 12);
        gtk_widget_set_margin_bottom(header, 6);
        gtk_widget_set_margin_start(header, 12);
        gtk_list_box_row_set_header(row, header);
        return;
    }

    gboolean was_rec = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(before), "is-recommended"));
    if (was_rec && !is_rec) {
        GtkWidget *header = gtk_label_new("Other Applications");
        gtk_widget_add_css_class(header, "dim-label");
        gtk_widget_set_halign(header, GTK_ALIGN_START);
        gtk_widget_set_margin_top(header, 12);
        gtk_widget_set_margin_bottom(header, 6);
        gtk_widget_set_margin_start(header, 12);
        gtk_list_box_row_set_header(row, header);
    } else {
        gtk_list_box_row_set_header(row, NULL);
    }
}

static void on_search_changed(GtkEditable *editable, AetherAppChooserDialog *self) {
    gtk_list_box_invalidate_filter(GTK_LIST_BOX(self->list_box));
}

static void populate_apps(AetherAppChooserDialog *self) {
    char *content_type = NULL;
    if (self->file) {
        GFileInfo *info = g_file_query_info(self->file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE, G_FILE_QUERY_INFO_NONE, NULL, NULL);
        if (info) {
            content_type = g_strdup(g_file_info_get_content_type(info));
            g_object_unref(info);
        }
    }

    GList *recommended = NULL;
    if (content_type) {
        recommended = g_app_info_get_recommended_for_type(content_type);
        g_free(content_type);
    }

    GList *apps = g_app_info_get_all();
    
    for (GList *l = apps; l != NULL; l = l->next) {
        GAppInfo *app = G_APP_INFO(l->data);
        if (g_app_info_should_show(app)) {
            gboolean is_recommended = FALSE;
            for (GList *r = recommended; r != NULL; r = r->next) {
                if (g_app_info_equal(app, G_APP_INFO(r->data))) {
                    is_recommended = TRUE;
                    break;
                }
            }
            GtkWidget *row = create_app_row(app);
            g_object_set_data(G_OBJECT(row), "is-recommended", GINT_TO_POINTER(is_recommended ? 1 : 0));
            gtk_list_box_append(GTK_LIST_BOX(self->list_box), row);
        }
    }
    g_list_free_full(apps, g_object_unref);
    if (recommended) g_list_free_full(recommended, g_object_unref);

    gtk_list_box_set_sort_func(GTK_LIST_BOX(self->list_box), sort_apps, NULL, NULL);
    gtk_list_box_set_filter_func(GTK_LIST_BOX(self->list_box), filter_apps, self, NULL);
    gtk_list_box_set_header_func(GTK_LIST_BOX(self->list_box), header_func, NULL, NULL);
}

static void aether_app_chooser_dialog_class_init(AetherAppChooserDialogClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = aether_app_chooser_dialog_finalize;
}

static void aether_app_chooser_dialog_init(AetherAppChooserDialog *self) {
    gtk_window_set_title(GTK_WINDOW(self), "Open With");
    gtk_window_set_default_size(GTK_WINDOW(self), 450, 600);
    gtk_window_set_modal(GTK_WINDOW(self), TRUE);

    gtk_widget_add_css_class(GTK_WIDGET(self), "app-chooser-dialog");

    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, 
        "window.app-chooser-dialog { background-color: rgba(0,0,0,0.3); }\n"
        "window.app-chooser-dialog scrolledwindow, "
        "window.app-chooser-dialog viewport, "
        "window.app-chooser-dialog list { background-color: transparent; }");
    gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(provider);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(header, 24);
    gtk_widget_set_margin_bottom(header, 12);
    gtk_widget_set_margin_start(header, 24);
    gtk_widget_set_margin_end(header, 24);
    
    GtkWidget *title = gtk_label_new("Select Application");
    gtk_widget_add_css_class(title, "title-1");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);

    self->search_entry = gtk_search_entry_new();
    g_signal_connect(self->search_entry, "changed", G_CALLBACK(on_search_changed), self);

    gtk_box_append(GTK_BOX(header), title);
    gtk_box_append(GTK_BOX(header), self->search_entry);

    self->list_box = gtk_list_box_new();
    gtk_widget_set_margin_start(self->list_box, 12);
    gtk_widget_set_margin_end(self->list_box, 12);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->list_box), GTK_SELECTION_SINGLE);
    g_signal_connect(self->list_box, "row-activated", G_CALLBACK(on_app_selected), self);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), self->list_box);

    gtk_box_append(GTK_BOX(vbox), header);
    gtk_box_append(GTK_BOX(vbox), scroll);

    gtk_window_set_child(GTK_WINDOW(self), vbox);
}

AetherAppChooserDialog *aether_app_chooser_dialog_new(GtkWindow *parent, GFile *file) {
    AetherAppChooserDialog *self = g_object_new(AETHER_TYPE_APP_CHOOSER_DIALOG, "transient-for", parent, NULL);
    self->file = g_object_ref(file);
    populate_apps(self);
    return self;
}
