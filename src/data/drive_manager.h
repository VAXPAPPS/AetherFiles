#ifndef AETHER_DRIVE_MANAGER_H
#define AETHER_DRIVE_MANAGER_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define AETHER_TYPE_DRIVE_MANAGER (aether_drive_manager_get_type())
G_DECLARE_FINAL_TYPE(AetherDriveManager, aether_drive_manager, AETHER, DRIVE_MANAGER, GObject)

AetherDriveManager *aether_drive_manager_new(void);

GListStore *aether_drive_manager_get_drives(AetherDriveManager *self);

void aether_drive_manager_mount_async(AetherDriveManager *self, GVolume *volume, GCancellable *cancellable, GAsyncReadyCallback callback, gpointer user_data);
gboolean aether_drive_manager_mount_finish(AetherDriveManager *self, GAsyncResult *res, GError **error);

G_END_DECLS

#endif /* AETHER_DRIVE_MANAGER_H */
