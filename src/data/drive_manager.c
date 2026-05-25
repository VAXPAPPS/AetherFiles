#include "drive_manager.h"
#include "../domain/drive_entity.h"
#include <gtk/gtk.h>

struct _AetherDriveManager {
    GObject parent_instance;

    GVolumeMonitor *monitor;
    GListStore *drives_store;
};

G_DEFINE_TYPE(AetherDriveManager, aether_drive_manager, G_TYPE_OBJECT)

enum {
    SIGNAL_DRIVES_CHANGED,
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void reload_drives(AetherDriveManager *self) {
    g_list_store_remove_all(self->drives_store);

    /* 1. Add all unmounted volumes */
    GList *volumes = g_volume_monitor_get_volumes(self->monitor);
    for (GList *l = volumes; l != NULL; l = l->next) {
        GVolume *vol = G_VOLUME(l->data);
        GMount *mount = g_volume_get_mount(vol);
        
        char *name = g_volume_get_name(vol);
        char *icon_name = "drive-harddisk-symbolic";
        GIcon *icon = g_volume_get_icon(vol);
        if (G_IS_THEMED_ICON(icon)) {
            const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
            if (names && names[0]) icon_name = (char *)names[0];
        }
        
        char *path = NULL;
        if (mount) {
            GFile *root = g_mount_get_root(mount);
            path = g_file_get_path(root);
            g_object_unref(root);
        }

        AetherDriveEntity *entity = aether_drive_entity_new(name, icon_name, mount != NULL, path, vol, mount);
        g_list_store_append(self->drives_store, entity);
        g_object_unref(entity);

        g_free(name);
        g_free(path);
        if (icon) g_object_unref(icon);
        if (mount) g_object_unref(mount);
    }
    g_list_free_full(volumes, g_object_unref);

    /* 2. Add mounts that are not associated with a volume (like network shares, bind mounts, etc.) */
    GList *mounts = g_volume_monitor_get_mounts(self->monitor);
    for (GList *l = mounts; l != NULL; l = l->next) {
        GMount *mount = G_MOUNT(l->data);
        GVolume *vol = g_mount_get_volume(mount);
        
        /* If it has a volume, we already added it above */
        if (!vol) {
            char *name = g_mount_get_name(mount);
            char *icon_name = "folder-remote-symbolic";
            GIcon *icon = g_mount_get_icon(mount);
            if (G_IS_THEMED_ICON(icon)) {
                const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
                if (names && names[0]) icon_name = (char *)names[0];
            }
            
            GFile *root = g_mount_get_root(mount);
            char *path = g_file_get_path(root);
            g_object_unref(root);

            /* We don't want to clutter with / or /boot etc. We can filter out some known non-removables if needed, but the user requested all for now */
            if (path && g_strcmp0(path, "/") != 0) {
                AetherDriveEntity *entity = aether_drive_entity_new(name, icon_name, TRUE, path, NULL, mount);
                g_list_store_append(self->drives_store, entity);
                g_object_unref(entity);
            }
            g_free(name);
            g_free(path);
            if (icon) g_object_unref(icon);
        } else {
            g_object_unref(vol);
        }
    }
    g_list_free_full(mounts, g_object_unref);

    g_signal_emit(self, signals[SIGNAL_DRIVES_CHANGED], 0);
}

static void on_monitor_changed(GVolumeMonitor *monitor, gpointer data, AetherDriveManager *self) {
    (void)monitor; (void)data;
    reload_drives(self);
}

static void aether_drive_manager_dispose(GObject *object) {
    AetherDriveManager *self = AETHER_DRIVE_MANAGER(object);
    if (self->monitor) {
        g_signal_handlers_disconnect_by_data(self->monitor, self);
        g_clear_object(&self->monitor);
    }
    g_clear_object(&self->drives_store);
    G_OBJECT_CLASS(aether_drive_manager_parent_class)->dispose(object);
}

static void aether_drive_manager_class_init(AetherDriveManagerClass *klass) {
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    oclass->dispose = aether_drive_manager_dispose;

    signals[SIGNAL_DRIVES_CHANGED] =
        g_signal_new("drives-changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_LAST,
                     0, NULL, NULL, NULL,
                     G_TYPE_NONE, 0);
}

static void aether_drive_manager_init(AetherDriveManager *self) {
    self->drives_store = g_list_store_new(AETHER_TYPE_DRIVE_ENTITY);
    self->monitor = g_volume_monitor_get();

    g_signal_connect(self->monitor, "volume-added", G_CALLBACK(on_monitor_changed), self);
    g_signal_connect(self->monitor, "volume-removed", G_CALLBACK(on_monitor_changed), self);
    g_signal_connect(self->monitor, "volume-changed", G_CALLBACK(on_monitor_changed), self);
    g_signal_connect(self->monitor, "mount-added", G_CALLBACK(on_monitor_changed), self);
    g_signal_connect(self->monitor, "mount-removed", G_CALLBACK(on_monitor_changed), self);
    g_signal_connect(self->monitor, "mount-changed", G_CALLBACK(on_monitor_changed), self);

    reload_drives(self);
}

AetherDriveManager *aether_drive_manager_new(void) {
    return g_object_new(AETHER_TYPE_DRIVE_MANAGER, NULL);
}

GListStore *aether_drive_manager_get_drives(AetherDriveManager *self) {
    return self->drives_store;
}

void aether_drive_manager_mount_async(AetherDriveManager *self, GVolume *volume, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data) {
    (void)self;
    GMountOperation *op = gtk_mount_operation_new(NULL);
    g_volume_mount(volume, G_MOUNT_MOUNT_NONE, op, cancellable, callback, user_data);
    g_object_unref(op);
}

gboolean aether_drive_manager_mount_finish(AetherDriveManager *self, GAsyncResult *res, GError **error) {
    (void)self;
    GVolume *volume = G_VOLUME(g_async_result_get_source_object(res));
    gboolean success = g_volume_mount_finish(volume, res, error);
    g_object_unref(volume);
    return success;
}
