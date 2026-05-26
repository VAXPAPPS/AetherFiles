#include "archive_manager.h"

struct _AetherArchiveManager {
    GObject parent_instance;
};

G_DEFINE_TYPE(AetherArchiveManager, aether_archive_manager, G_TYPE_OBJECT)

static AetherArchiveManager *default_instance = NULL;

static void aether_archive_manager_class_init(AetherArchiveManagerClass *klass) {
    (void)klass;
}

static void aether_archive_manager_init(AetherArchiveManager *self) {
    (void)self;
}

AetherArchiveManager *aether_archive_manager_get_default(void) {
    if (!default_instance) {
        default_instance = g_object_new(AETHER_TYPE_ARCHIVE_MANAGER, NULL);
    }
    return default_instance;
}

static void on_subprocess_exited(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GTask *task = G_TASK(user_data);
    GSubprocess *proc = G_SUBPROCESS(source_object);
    GError *err = NULL;
    
    g_subprocess_wait_finish(proc, res, &err);
    if (err) {
        g_task_return_error(task, err);
    } else {
        if (g_subprocess_get_if_exited(proc) && g_subprocess_get_exit_status(proc) == 0) {
            g_task_return_boolean(task, TRUE);
        } else {
            g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "Archiver process failed or exited with error.");
        }
    }
    
    g_object_unref(task);
}

void aether_archive_manager_extract_async(AetherArchiveManager *self,
                                          const char *archive_path,
                                          const char *dest_dir,
                                          GAsyncReadyCallback callback,
                                          gpointer user_data)
{
    GTask *task = g_task_new(self, NULL, callback, user_data);
    
    GPtrArray *cmd = g_ptr_array_new_with_free_func(g_free);
    
    if (g_str_has_suffix(archive_path, ".zip")) {
        g_ptr_array_add(cmd, g_strdup("unzip"));
        g_ptr_array_add(cmd, g_strdup("-q"));
        g_ptr_array_add(cmd, g_strdup(archive_path));
        g_ptr_array_add(cmd, g_strdup("-d"));
        g_ptr_array_add(cmd, g_strdup(dest_dir));
    } else if (g_str_has_suffix(archive_path, ".tar.xz") || g_str_has_suffix(archive_path, ".tar.gz") || g_str_has_suffix(archive_path, ".tar")) {
        g_ptr_array_add(cmd, g_strdup("tar"));
        g_ptr_array_add(cmd, g_strdup("-xf"));
        g_ptr_array_add(cmd, g_strdup(archive_path));
        g_ptr_array_add(cmd, g_strdup("-C"));
        g_ptr_array_add(cmd, g_strdup(dest_dir));
    } else if (g_str_has_suffix(archive_path, ".7z")) {
        g_ptr_array_add(cmd, g_strdup("7z"));
        g_ptr_array_add(cmd, g_strdup("x"));
        g_ptr_array_add(cmd, g_strdup("-y"));
        g_ptr_array_add(cmd, g_strdup(archive_path));
        char *out_arg = g_strdup_printf("-o%s", dest_dir);
        g_ptr_array_add(cmd, out_arg);
    } else {
        g_ptr_array_free(cmd, TRUE);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unsupported archive format");
        g_object_unref(task);
        return;
    }
    g_ptr_array_add(cmd, NULL);
    
    GError *err = NULL;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, (const char * const *)cmd->pdata, &err);
    g_object_unref(launcher);
    g_ptr_array_free(cmd, TRUE);
    
    if (proc) {
        g_subprocess_wait_async(proc, NULL, on_subprocess_exited, task);
        g_object_unref(proc);
    } else {
        g_task_return_error(task, err);
        g_object_unref(task);
    }
}

gboolean aether_archive_manager_extract_finish(AetherArchiveManager *self,
                                               GAsyncResult *res,
                                               GError **error)
{
    (void)self;
    return g_task_propagate_boolean(G_TASK(res), error);
}

void aether_archive_manager_compress_async(AetherArchiveManager *self,
                                           GStrv source_paths,
                                           const char *dest_archive_path,
                                           const char *format,
                                           GAsyncReadyCallback callback,
                                           gpointer user_data)
{
    GTask *task = g_task_new(self, NULL, callback, user_data);
    
    if (!source_paths || !source_paths[0]) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "No source files provided");
        g_object_unref(task);
        return;
    }
    
    GPtrArray *cmd = g_ptr_array_new_with_free_func(g_free);
    
    if (g_strcmp0(format, "zip") == 0) {
        g_ptr_array_add(cmd, g_strdup("zip"));
        g_ptr_array_add(cmd, g_strdup("-rq"));
        g_ptr_array_add(cmd, g_strdup(dest_archive_path));
    } else if (g_strcmp0(format, "tar.xz") == 0) {
        g_ptr_array_add(cmd, g_strdup("tar"));
        g_ptr_array_add(cmd, g_strdup("-cJf"));
        g_ptr_array_add(cmd, g_strdup(dest_archive_path));
    } else if (g_strcmp0(format, "7z") == 0) {
        g_ptr_array_add(cmd, g_strdup("7z"));
        g_ptr_array_add(cmd, g_strdup("a"));
        g_ptr_array_add(cmd, g_strdup("-y"));
        g_ptr_array_add(cmd, g_strdup(dest_archive_path));
    } else {
        g_ptr_array_free(cmd, TRUE);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "Unsupported compression format");
        g_object_unref(task);
        return;
    }
    
    // We want to run the command in the parent directory of the first file
    // and pass only basenames to avoid archiving full absolute paths.
    char *cwd = g_path_get_dirname(source_paths[0]);
    
    for (int i = 0; source_paths[i] != NULL; i++) {
        char *basename = g_path_get_basename(source_paths[i]);
        g_ptr_array_add(cmd, basename);
    }
    g_ptr_array_add(cmd, NULL);
    
    GError *err = NULL;
    GSubprocessLauncher *launcher = g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);
    g_subprocess_launcher_set_cwd(launcher, cwd);
    
    GSubprocess *proc = g_subprocess_launcher_spawnv(launcher, (const char * const *)cmd->pdata, &err);
    g_object_unref(launcher);
    g_ptr_array_free(cmd, TRUE);
    g_free(cwd);
    
    if (proc) {
        g_subprocess_wait_async(proc, NULL, on_subprocess_exited, task);
        g_object_unref(proc);
    } else {
        g_task_return_error(task, err);
        g_object_unref(task);
    }
}

gboolean aether_archive_manager_compress_finish(AetherArchiveManager *self,
                                                GAsyncResult *res,
                                                GError **error)
{
    (void)self;
    return g_task_propagate_boolean(G_TASK(res), error);
}
