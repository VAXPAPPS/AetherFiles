#include "bluetooth_device_entity.h"

struct _AetherBluetoothDeviceEntity {
    GObject parent_instance;
    char *name;
    char *address;
    char *object_path;
    gboolean paired;
    gboolean trusted;
};

G_DEFINE_TYPE(AetherBluetoothDeviceEntity, aether_bluetooth_device_entity, G_TYPE_OBJECT)

static void aether_bluetooth_device_entity_finalize(GObject *object) {
    AetherBluetoothDeviceEntity *self = AETHER_BLUETOOTH_DEVICE_ENTITY(object);
    g_free(self->name);
    g_free(self->address);
    g_free(self->object_path);
    G_OBJECT_CLASS(aether_bluetooth_device_entity_parent_class)->finalize(object);
}

static void aether_bluetooth_device_entity_class_init(AetherBluetoothDeviceEntityClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = aether_bluetooth_device_entity_finalize;
}

static void aether_bluetooth_device_entity_init(AetherBluetoothDeviceEntity *self) {
}

AetherBluetoothDeviceEntity *aether_bluetooth_device_entity_new(const char *name, const char *address, const char *object_path, gboolean paired, gboolean trusted) {
    AetherBluetoothDeviceEntity *self = g_object_new(AETHER_TYPE_BLUETOOTH_DEVICE_ENTITY, NULL);
    self->name = g_strdup(name);
    self->address = g_strdup(address);
    self->object_path = g_strdup(object_path);
    self->paired = paired;
    self->trusted = trusted;
    return self;
}

const char *aether_bluetooth_device_entity_get_name(AetherBluetoothDeviceEntity *self) { return self->name; }
const char *aether_bluetooth_device_entity_get_address(AetherBluetoothDeviceEntity *self) { return self->address; }
const char *aether_bluetooth_device_entity_get_object_path(AetherBluetoothDeviceEntity *self) { return self->object_path; }
gboolean    aether_bluetooth_device_entity_get_paired(AetherBluetoothDeviceEntity *self) { return self->paired; }
gboolean    aether_bluetooth_device_entity_get_trusted(AetherBluetoothDeviceEntity *self) { return self->trusted; }
