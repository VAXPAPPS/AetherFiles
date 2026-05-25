#include "drive_entity.h"

struct _AetherDriveEntity {
    GObject parent_instance;

    char *name;
    char *icon_name;
    gboolean is_mounted;
    char *path;

    GVolume *volume;
    GMount *mount;
};

G_DEFINE_TYPE(AetherDriveEntity, aether_drive_entity, G_TYPE_OBJECT)

static void aether_drive_entity_dispose(GObject *object) {
    AetherDriveEntity *self = AETHER_DRIVE_ENTITY(object);
    g_clear_object(&self->volume);
    g_clear_object(&self->mount);
    G_OBJECT_CLASS(aether_drive_entity_parent_class)->dispose(object);
}

static void aether_drive_entity_finalize(GObject *object) {
    AetherDriveEntity *self = AETHER_DRIVE_ENTITY(object);
    g_free(self->name);
    g_free(self->icon_name);
    g_free(self->path);
    G_OBJECT_CLASS(aether_drive_entity_parent_class)->finalize(object);
}

static void aether_drive_entity_class_init(AetherDriveEntityClass *klass) {
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    oclass->dispose = aether_drive_entity_dispose;
    oclass->finalize = aether_drive_entity_finalize;
}

static void aether_drive_entity_init(AetherDriveEntity *self) {
    self->name = NULL;
    self->icon_name = NULL;
    self->is_mounted = FALSE;
    self->path = NULL;
    self->volume = NULL;
    self->mount = NULL;
}

AetherDriveEntity *aether_drive_entity_new(const char *name, const char *icon_name, gboolean is_mounted, const char *path, GVolume *volume, GMount *mount) {
    AetherDriveEntity *self = g_object_new(AETHER_TYPE_DRIVE_ENTITY, NULL);
    self->name = g_strdup(name);
    self->icon_name = g_strdup(icon_name);
    self->is_mounted = is_mounted;
    self->path = g_strdup(path);
    if (volume) self->volume = g_object_ref(volume);
    if (mount) self->mount = g_object_ref(mount);
    return self;
}

const char *aether_drive_entity_get_name(AetherDriveEntity *self) { return self->name; }
const char *aether_drive_entity_get_icon_name(AetherDriveEntity *self) { return self->icon_name; }
gboolean    aether_drive_entity_is_mounted(AetherDriveEntity *self) { return self->is_mounted; }
const char *aether_drive_entity_get_path(AetherDriveEntity *self) { return self->path; }
GVolume    *aether_drive_entity_get_volume(AetherDriveEntity *self) { return self->volume; }
GMount     *aether_drive_entity_get_mount(AetherDriveEntity *self) { return self->mount; }
