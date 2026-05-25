#include "gio_file_repository.h"
#include "../domain/file_entity.h"

struct _AetherGioFileRepository {
    GObject parent_instance;
};

static void aether_gio_file_repository_iface_init(AetherFileRepositoryInterface *iface);

G_DEFINE_TYPE_WITH_CODE(AetherGioFileRepository, aether_gio_file_repository, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(AETHER_TYPE_FILE_REPOSITORY, aether_gio_file_repository_iface_init))

static void aether_gio_file_repository_init(AetherGioFileRepository *self) {
}

static void aether_gio_file_repository_class_init(AetherGioFileRepositoryClass *klass) {
}

AetherGioFileRepository *aether_gio_file_repository_new(void) {
    return g_object_new(AETHER_TYPE_GIO_FILE_REPOSITORY, NULL);
}

typedef struct {
    GTask *task;
    GListStore *store;
} ListDirData;

static void list_dir_data_free(ListDirData *data) {
    g_object_unref(data->task);
    g_object_unref(data->store);
    g_free(data);
}

static void on_enumerate_children_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GFile *file = G_FILE(source_object);
    GError *error = NULL;
    GFileEnumerator *enumerator = g_file_enumerate_children_finish(file, res, &error);
    
    ListDirData *data = user_data;
    
    if (error) {
        g_task_return_error(data->task, error);
        list_dir_data_free(data);
        return;
    }
    
    while (TRUE) {
        GFileInfo *info = g_file_enumerator_next_file(enumerator, NULL, NULL);
        if (!info) break;
        
        const char *name = g_file_info_get_name(info);
        gboolean is_dir = g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY;
        goffset size = g_file_info_get_size(info);
        const char *icon_name = NULL;
        GIcon *icon = g_file_info_get_icon(info);
        
        if (G_IS_THEMED_ICON(icon)) {
            const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
            if (names && names[0]) icon_name = names[0];
        }
        
        if (!icon_name) {
            icon_name = is_dir ? "folder" : "text-x-generic";
        }
        
        GFile *child = g_file_enumerator_get_child(enumerator, info);
        char *path = g_file_get_path(child);
        char *uri = g_file_get_uri(child);
        
        AetherFileEntity *entity = aether_file_entity_new(name, path, uri, size, is_dir, icon_name);
        g_list_store_append(data->store, entity);
        
        g_object_unref(entity);
        g_free(path);
        g_free(uri);
        g_object_unref(child);
        g_object_unref(info);
    }
    
    g_object_unref(enumerator);
    
    g_task_return_pointer(data->task, g_object_ref(data->store), g_object_unref);
    list_dir_data_free(data);
}

static void gio_list_directory_async(AetherFileRepository *repo, const char *path, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
    GTask *task = g_task_new(repo, cancellable, callback, user_data);
    
    GFile *file = g_file_parse_name(path);
    
    ListDirData *data = g_new0(ListDirData, 1);
    data->task = task;
    data->store = g_list_store_new(AETHER_TYPE_FILE_ENTITY);
    
    g_file_enumerate_children_async(file,
                                    G_FILE_ATTRIBUTE_STANDARD_NAME ","
                                    G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                    G_FILE_ATTRIBUTE_STANDARD_SIZE ","
                                    G_FILE_ATTRIBUTE_STANDARD_ICON,
                                    G_FILE_QUERY_INFO_NONE,
                                    G_PRIORITY_DEFAULT,
                                    cancellable,
                                    on_enumerate_children_ready,
                                    data);
    
    g_object_unref(file);
}

static GListModel *gio_list_directory_finish(AetherFileRepository *repo, GAsyncResult *res, GError **error) {
    g_return_val_if_fail(g_task_is_valid(res, repo), NULL);
    return g_task_propagate_pointer(G_TASK(res), error);
}

static void aether_gio_file_repository_iface_init(AetherFileRepositoryInterface *iface) {
    iface->list_directory_async = gio_list_directory_async;
    iface->list_directory_finish = gio_list_directory_finish;
}
