#pragma once
#include <gio/gio.h>
#include "../domain/bluetooth_device_entity.h"

#define AETHER_TYPE_BLUEZ_DBUS_MANAGER (aether_bluez_dbus_manager_get_type())
G_DECLARE_FINAL_TYPE(AetherBluezDbusManager, aether_bluez_dbus_manager, AETHER, BLUEZ_DBUS_MANAGER, GObject)

AetherBluezDbusManager *aether_bluez_dbus_manager_get_default(void);

/* Returns a GList of AetherBluetoothDeviceEntity* */
GList *aether_bluez_dbus_manager_get_devices(AetherBluezDbusManager *self);

typedef void (*AetherBluezProgressCallback)(const char *status, guint64 transferred, guint64 size, gpointer user_data);

/* Sends a file to the specified device using OBEX.
   progress_callback is optional. callback will be called when transfer completes (or fails). */
void aether_bluez_dbus_manager_send_file_async(AetherBluezDbusManager *self, const char *device_address, const char *file_path, AetherBluezProgressCallback progress_callback, gpointer progress_user_data, GAsyncReadyCallback callback, gpointer user_data);

gboolean aether_bluez_dbus_manager_send_file_finish(AetherBluezDbusManager *self, GAsyncResult *res, GError **error);
