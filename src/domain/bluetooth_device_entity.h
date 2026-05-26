#pragma once
#include <glib-object.h>

#define AETHER_TYPE_BLUETOOTH_DEVICE_ENTITY (aether_bluetooth_device_entity_get_type())
G_DECLARE_FINAL_TYPE(AetherBluetoothDeviceEntity, aether_bluetooth_device_entity, AETHER, BLUETOOTH_DEVICE_ENTITY, GObject)

AetherBluetoothDeviceEntity *aether_bluetooth_device_entity_new(const char *name, const char *address, const char *object_path, gboolean paired, gboolean trusted);

const char *aether_bluetooth_device_entity_get_name(AetherBluetoothDeviceEntity *self);
const char *aether_bluetooth_device_entity_get_address(AetherBluetoothDeviceEntity *self);
const char *aether_bluetooth_device_entity_get_object_path(AetherBluetoothDeviceEntity *self);
gboolean    aether_bluetooth_device_entity_get_paired(AetherBluetoothDeviceEntity *self);
gboolean    aether_bluetooth_device_entity_get_trusted(AetherBluetoothDeviceEntity *self);
