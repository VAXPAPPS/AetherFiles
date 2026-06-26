#include "external_drop_controller.h"
#include "../views/window_private.h"
#include <gio/gio.h>
#include <string.h>

/* ── Helpers ── */

static char *get_unique_filename(const char *parent_dir, const char *base_name) {
    char *dest_path = g_build_filename(parent_dir, base_name, NULL);
    char *unique_path = g_strdup(dest_path);
    int counter = 1;

    while (g_file_test(unique_path, G_FILE_TEST_EXISTS)) {
        g_free(unique_path);
        char *name_no_ext = g_strdup(base_name);
        char *dot = strrchr(name_no_ext, '.');
        if (dot) *dot = '\0';
        const char *ext = dot ? strrchr(base_name, '.') : "";
        char *new_name = g_strdup_printf("%s (%d)%s", name_no_ext, counter++, ext);
        unique_path = g_build_filename(parent_dir, new_name, NULL);
        g_free(name_no_ext);
        g_free(new_name);
    }
    g_free(dest_path);
    return unique_path;
}

static gboolean is_media_or_document(const char *url) {
    const char *exts[] = { ".jpg", ".jpeg", ".png", ".gif", ".webp", ".mp4", ".mp3", ".pdf", ".zip", ".tar.gz", ".xz", ".iso", ".txt", ".md", NULL };
    char *lower = g_ascii_strdown(url, -1);
    gboolean is_file = FALSE;
    for (int i = 0; exts[i] != NULL; i++) {
        if (g_str_has_suffix(lower, exts[i])) {
            is_file = TRUE;
            break;
        }
    }
    g_free(lower);
    return is_file;
}

static void create_text_snippet(AetherWindow *win, const char *text) {
    char *unique_path = get_unique_filename(win->current_path, "Dropped Snippet.txt");
    GError *err = NULL;
    if (!g_file_set_contents(unique_path, text, -1, &err)) {
        g_printerr("Failed to save snippet: %s\n", err->message);
        g_error_free(err);
    }
    g_free(unique_path);
    aether_window_reload(win);
}

static void create_web_shortcut(AetherWindow *win, const char *url) {
    char *unique_path = get_unique_filename(win->current_path, "Web Link.desktop");
    char *content = g_strdup_printf("[Desktop Entry]\nType=Link\nURL=%s\nIcon=text-html\nName=Web Link\n", url);
    g_file_set_contents(unique_path, content, -1, NULL);
    g_free(content);
    g_free(unique_path);
    aether_window_reload(win);
}

/* ── Download Task ── */

typedef struct {
    AetherWindow *win;
    GFile *src_file;
    GFile *dest_file;
    GCancellable *cancellable;
} DownloadTask;

static void on_download_progress(goffset current_num_bytes, goffset total_num_bytes, gpointer user_data) {
    /* For future: update a progress bar in AetherWindow */
    (void)current_num_bytes; (void)total_num_bytes; (void)user_data;
}

static void on_download_complete(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    DownloadTask *dt = user_data;
    GError *err = NULL;
    g_file_copy_finish(G_FILE(source_object), res, &err);
    
    if (err) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(dt->win),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                                   "Download Failed");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", err->message);
        g_signal_connect(dialog, "response", G_CALLBACK(gtk_window_destroy), NULL);
        gtk_window_present(GTK_WINDOW(dialog));
        g_error_free(err);
        
        /* Cleanup partial file */
        g_file_delete(dt->dest_file, NULL, NULL);
    } else {
        aether_window_reload(dt->win);
    }

    if (dt->win->progress_spinner) {
        gtk_spinner_stop(GTK_SPINNER(dt->win->progress_spinner));
    }
    
    g_object_unref(dt->src_file);
    g_object_unref(dt->dest_file);
    if (dt->cancellable) g_object_unref(dt->cancellable);
    g_free(dt);
}

static void download_file_from_url(AetherWindow *win, const char *url) {
    GFile *src = g_file_new_for_uri(url);
    char *basename = g_path_get_basename(url);
    char *unique_path = get_unique_filename(win->current_path, basename);
    GFile *dest = g_file_new_for_path(unique_path);
    
    DownloadTask *dt = g_new0(DownloadTask, 1);
    dt->win = win;
    dt->src_file = src;
    dt->dest_file = dest;
    dt->cancellable = g_cancellable_new();

    if (win->progress_spinner) {
        gtk_spinner_start(GTK_SPINNER(win->progress_spinner));
    }

    g_file_copy_async(src, dest,
                      G_FILE_COPY_OVERWRITE,
                      G_PRIORITY_DEFAULT,
                      dt->cancellable,
                      on_download_progress, dt,
                      on_download_complete, dt);

    g_free(basename);
    g_free(unique_path);
}

/* ── Dialog Handlers ── */

typedef struct {
    AetherWindow *win;
    char *url;
} LinkDropData;

static void on_link_drop_response(GtkDialog *dialog, int response, gpointer user_data) {
    LinkDropData *data = user_data;
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (response == GTK_RESPONSE_ACCEPT) {
        download_file_from_url(data->win, data->url);
    } else if (response == GTK_RESPONSE_REJECT) {
        create_web_shortcut(data->win, data->url);
    }
    
    g_free(data->url);
    g_free(data);
}

static void handle_url_drop(AetherWindow *win, const char *url) {
    if (is_media_or_document(url)) {
        GtkWidget *dialog = gtk_message_dialog_new(GTK_WINDOW(win),
                                                   GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                   GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                                   "Download File?");
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
                                                 "You dropped a link to a file/image.\nWould you like to download it, or just create a shortcut?");
        
        gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL);
        gtk_dialog_add_button(GTK_DIALOG(dialog), "Create Shortcut", GTK_RESPONSE_REJECT);
        GtkWidget *btn = gtk_dialog_add_button(GTK_DIALOG(dialog), "Download", GTK_RESPONSE_ACCEPT);
        gtk_widget_add_css_class(btn, "suggested-action");
        
        LinkDropData *data = g_new0(LinkDropData, 1);
        data->win = win;
        data->url = g_strdup(url);
        
        g_signal_connect(dialog, "response", G_CALLBACK(on_link_drop_response), data);
        gtk_window_present(GTK_WINDOW(dialog));
    } else {
        create_web_shortcut(win, url);
    }
}

/* ── Main Handler ── */

gboolean aether_external_drop_handle(AetherWindow *win, const GValue *value) {
    if (!win->current_path) return FALSE;

    if (G_VALUE_HOLDS(value, G_TYPE_STRING)) {
        const char *text = g_value_get_string(value);
        if (!text) return FALSE;

        /* Clean up trailing newlines often added by browsers */
        char *cleaned = g_strdup(text);
        g_strstrip(cleaned);

        if (g_str_has_prefix(cleaned, "http://") || g_str_has_prefix(cleaned, "https://")) {
            handle_url_drop(win, cleaned);
        } else {
            create_text_snippet(win, text);
        }
        g_free(cleaned);
        return TRUE;
    }

    if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
        GdkFileList *file_list = g_value_get_boxed(value);
        if (!file_list) return FALSE;

        GSList *files = gdk_file_list_get_files(file_list);
        gboolean handled_any = FALSE;

        for (GSList *l = files; l != NULL; l = l->next) {
            GFile *src_file = G_FILE(l->data);
            char *path = g_file_get_path(src_file);
            
            /* If path is NULL, it's a URI from an external app (like a browser dragging an image) */
            if (!path) {
                char *uri = g_file_get_uri(src_file);
                if (uri && (g_str_has_prefix(uri, "http://") || g_str_has_prefix(uri, "https://"))) {
                    handle_url_drop(win, uri);
                    handled_any = TRUE;
                }
                g_free(uri);
            } else {
                g_free(path);
            }
        }
        
        /* If we handled URIs, we return TRUE so that we process them. 
           Wait, if there are mixed local files and URIs, we should only return TRUE if we 
           want to consume the entire drop. For now, returning handled_any is safe if all are URIs.
           Actually, the local file drag and drop logic expects paths. We will let the caller handle local files. */
        return handled_any;
    }

    return FALSE;
}
