#ifndef AETHER_DRIVE_ENTITY_H
#define AETHER_DRIVE_ENTITY_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define AETHER_TYPE_DRIVE_ENTITY (aether_drive_entity_get_type())
G_DECLARE_FINAL_TYPE(AetherDriveEntity, aether_drive_entity, AETHER, DRIVE_ENTITY, GObject)

AetherDriveEntity *aether_drive_entity_new(const char *name, const char *icon_name, gboolean is_mounted, const char *path, GVolume *volume, GMount *mount);

const char *aether_drive_entity_get_name(AetherDriveEntity *self);
const char *aether_drive_entity_get_icon_name(AetherDriveEntity *self);
gboolean    aether_drive_entity_is_mounted(AetherDriveEntity *self);
const char *aether_drive_entity_get_path(AetherDriveEntity *self);
GVolume    *aether_drive_entity_get_volume(AetherDriveEntity *self);
GMount     *aether_drive_entity_get_mount(AetherDriveEntity *self);

G_END_DECLS

#endif /* AETHER_DRIVE_ENTITY_H */
