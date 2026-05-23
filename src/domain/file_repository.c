#include "file_repository.h"

G_DEFINE_INTERFACE(AetherFileRepository, aether_file_repository, G_TYPE_OBJECT)

static void aether_file_repository_default_init(AetherFileRepositoryInterface *iface) {
}

void aether_file_repository_list_directory_async(AetherFileRepository *self, const char *path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
    AETHER_FILE_REPOSITORY_GET_IFACE(self)->list_directory_async(self, path, cancellable, callback, user_data);
}

GListModel *aether_file_repository_list_directory_finish(AetherFileRepository *self, GAsyncResult *res, GError **error) {
    return AETHER_FILE_REPOSITORY_GET_IFACE(self)->list_directory_finish(self, res, error);
}
