#ifndef AETHER_THUMBNAIL_MANAGER_H
#define AETHER_THUMBNAIL_MANAGER_H

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define AETHER_TYPE_THUMBNAIL_MANAGER (aether_thumbnail_manager_get_type())
G_DECLARE_FINAL_TYPE(AetherThumbnailManager, aether_thumbnail_manager, AETHER, THUMBNAIL_MANAGER, GObject)

AetherThumbnailManager *aether_thumbnail_manager_get_default(void);

/* Request a thumbnail asynchronously. The callback receives a GdkTexture* (or NULL on failure). */
void aether_thumbnail_manager_get_thumbnail_async(
    AetherThumbnailManager *self,
    const char *uri,
    const char *mime_type,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data);

GdkTexture *aether_thumbnail_manager_get_thumbnail_finish(
    AetherThumbnailManager *self,
    GAsyncResult *res,
    GError **error);

G_END_DECLS

#endif /* AETHER_THUMBNAIL_MANAGER_H */
