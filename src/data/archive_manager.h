#ifndef AETHER_ARCHIVE_MANAGER_H
#define AETHER_ARCHIVE_MANAGER_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define AETHER_TYPE_ARCHIVE_MANAGER (aether_archive_manager_get_type())
G_DECLARE_FINAL_TYPE(AetherArchiveManager, aether_archive_manager, AETHER, ARCHIVE_MANAGER, GObject)

AetherArchiveManager *aether_archive_manager_get_default(void);

void aether_archive_manager_extract_async(AetherArchiveManager *self,
                                          const char *archive_path,
                                          const char *dest_dir,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data);

gboolean aether_archive_manager_extract_finish(AetherArchiveManager *self,
                                               GAsyncResult *res,
                                               GError **error);

void aether_archive_manager_compress_async(AetherArchiveManager *self,
                                           GStrv source_paths,
                                           const char *dest_archive_path,
                                           const char *format,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data);

gboolean aether_archive_manager_compress_finish(AetherArchiveManager *self,
                                                GAsyncResult *res,
                                                GError **error);

G_END_DECLS

#endif /* AETHER_ARCHIVE_MANAGER_H */
