#pragma once
#include <glib-object.h>
#include <gio/gio.h>
#include "file_entity.h"

#define AETHER_TYPE_FILE_REPOSITORY (aether_file_repository_get_type())
G_DECLARE_INTERFACE(AetherFileRepository, aether_file_repository, AETHER, FILE_REPOSITORY, GObject)

struct _AetherFileRepositoryInterface {
    GTypeInterface parent_iface;
    
    void (*list_directory_async)(AetherFileRepository *self, const char *path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
    GListModel *(*list_directory_finish)(AetherFileRepository *self, GAsyncResult *res, GError **error);
};

void aether_file_repository_list_directory_async(AetherFileRepository *self, const char *path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
GListModel *aether_file_repository_list_directory_finish(AetherFileRepository *self, GAsyncResult *res, GError **error);
