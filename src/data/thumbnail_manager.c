#include "thumbnail_manager.h"
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <stdlib.h>

struct _AetherThumbnailManager {
    GObject parent_instance;
};

G_DEFINE_TYPE(AetherThumbnailManager, aether_thumbnail_manager, G_TYPE_OBJECT)

static AetherThumbnailManager *default_instance = NULL;

static void aether_thumbnail_manager_class_init(AetherThumbnailManagerClass *klass) {
    (void)klass;
}

static void aether_thumbnail_manager_init(AetherThumbnailManager *self) {
    (void)self;
}

AetherThumbnailManager *aether_thumbnail_manager_get_default(void) {
    if (!default_instance) {
        default_instance = g_object_new(AETHER_TYPE_THUMBNAIL_MANAGER, NULL);
    }
    return default_instance;
}

typedef struct {
    char *uri;
    char *mime_type;
} ThumbnailRequest;

static void thumbnail_request_free(ThumbnailRequest *req) {
    g_free(req->uri);
    g_free(req->mime_type);
    g_free(req);
}

static char *get_thumbnail_path(const char *uri) {
    char *hash = g_compute_checksum_for_string(G_CHECKSUM_MD5, uri, -1);
    char *filename = g_strdup_printf("%s.png", hash);
    g_free(hash);
    
    const char *cache_dir = g_get_user_cache_dir();
    char *thumb_dir = g_build_filename(cache_dir, "thumbnails", "large", NULL);
    g_mkdir_with_parents(thumb_dir, 0700);
    
    char *thumb_path = g_build_filename(thumb_dir, filename, NULL);
    g_free(filename);
    g_free(thumb_dir);
    
    return thumb_path;
}

static GdkPixbuf *generate_video_thumbnail(const char *path) {
    /* Extract 1 frame at 10% or just the beginning */
    char *tmp_filename = g_strdup_printf("thumb_%p_%d.png", path, g_random_int());
    char *tmp_path = g_build_filename(g_get_tmp_dir(), tmp_filename, NULL);
    g_free(tmp_filename);
    
    char *argv[] = {
        "ffmpeg", "-y", "-i", (char *)path,
        "-vf", "thumbnail,scale=256:256:force_original_aspect_ratio=increase,crop=256:256",
        "-frames:v", "1",
        tmp_path, NULL
    };
    
    gboolean success = g_spawn_sync(NULL, argv, NULL,
                                    G_SPAWN_SEARCH_PATH | G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                                    NULL, NULL, NULL, NULL, NULL, NULL);
                                    
    GdkPixbuf *pixbuf = NULL;
    if (success) {
        pixbuf = gdk_pixbuf_new_from_file_at_scale(tmp_path, 256, 256, TRUE, NULL);
    }
    
    g_unlink(tmp_path);
    g_free(tmp_path);
    return pixbuf;
}

static GdkPixbuf *load_and_crop_image(const char *path) {
    int w = 0, h = 0;
    if (!gdk_pixbuf_get_file_info(path, &w, &h)) return NULL;
    if (w <= 0 || h <= 0) return NULL;
    
    int target_w = 256;
    int target_h = 256;
    if (w > h) {
        target_w = -1; /* Scale height to 256, let width be proportional */
    } else {
        target_h = -1; /* Scale width to 256, let height be proportional */
    }
    
    GError *err = NULL;
    GdkPixbuf *scaled = gdk_pixbuf_new_from_file_at_scale(path, target_w, target_h, TRUE, &err);
    if (!scaled) {
        if (err) g_error_free(err);
        return NULL;
    }
    
    int sw = gdk_pixbuf_get_width(scaled);
    int sh = gdk_pixbuf_get_height(scaled);
    
    int size = MIN(sw, sh);
    int x = MAX(0, (sw - size) / 2);
    int y = MAX(0, (sh - size) / 2);
    
    GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(scaled, x, y, size, size);
    GdkPixbuf *final = gdk_pixbuf_copy(sub);
    g_object_unref(sub);
    g_object_unref(scaled);
    
    return final;
}

static void thumbnail_thread_func(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    (void)source_object;
    ThumbnailRequest *req = task_data;
    
    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
        return;
    }
    
    char *thumb_path = get_thumbnail_path(req->uri);
    GdkPixbuf *pixbuf = NULL;
    
    /* 1. Check if cached */
    if (g_file_test(thumb_path, G_FILE_TEST_EXISTS)) {
        pixbuf = gdk_pixbuf_new_from_file(thumb_path, NULL);
    }
    
    /* 2. Generate if not cached */
    if (!pixbuf) {
        char *local_path = g_filename_from_uri(req->uri, NULL, NULL);
        if (local_path) {
            if (g_str_has_prefix(req->mime_type, "video/")) {
                pixbuf = generate_video_thumbnail(local_path);
            } else if (g_str_has_prefix(req->mime_type, "image/")) {
                pixbuf = load_and_crop_image(local_path);
            }
            g_free(local_path);
        }
        
        /* Save to cache */
        if (pixbuf) {
            gdk_pixbuf_save(pixbuf, thumb_path, "png", NULL, "tEXt::Thumb::URI", req->uri, NULL);
        }
    }
    g_free(thumb_path);
    
    if (g_cancellable_is_cancelled(cancellable)) {
        if (pixbuf) g_object_unref(pixbuf);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
        return;
    }
    
    if (pixbuf) {
        /* GdkTexture cannot be created from Pixbuf off the main thread in some older GTK versions,
           but in GTK4 gdk_texture_new_for_pixbuf is thread-safe! */
        GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
        g_object_unref(pixbuf);
        g_task_return_pointer(task, texture, g_object_unref);
    } else {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to generate thumbnail");
    }
}

void aether_thumbnail_manager_get_thumbnail_async(
    AetherThumbnailManager *self,
    const char *uri,
    const char *mime_type,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
    g_return_if_fail(AETHER_IS_THUMBNAIL_MANAGER(self));
    g_return_if_fail(uri != NULL);
    
    GTask *task = g_task_new(self, cancellable, callback, user_data);
    
    ThumbnailRequest *req = g_new0(ThumbnailRequest, 1);
    req->uri = g_strdup(uri);
    req->mime_type = g_strdup(mime_type ? mime_type : "");
    g_task_set_task_data(task, req, (GDestroyNotify)thumbnail_request_free);
    
    g_task_run_in_thread(task, thumbnail_thread_func);
    g_object_unref(task);
}

GdkTexture *aether_thumbnail_manager_get_thumbnail_finish(
    AetherThumbnailManager *self,
    GAsyncResult *res,
    GError **error)
{
    g_return_val_if_fail(AETHER_IS_THUMBNAIL_MANAGER(self), NULL);
    g_return_val_if_fail(g_task_is_valid(res, self), NULL);
    
    return g_task_propagate_pointer(G_TASK(res), error);
}
