#pragma once
#include <gtk/gtk.h>
#include "../../data/bluez_dbus_manager.h"

#define AETHER_TYPE_BLUETOOTH_SHARE_DIALOG (aether_bluetooth_share_dialog_get_type())
G_DECLARE_FINAL_TYPE(AetherBluetoothShareDialog, aether_bluetooth_share_dialog, AETHER, BLUETOOTH_SHARE_DIALOG, GtkWindow)

AetherBluetoothShareDialog *aether_bluetooth_share_dialog_new(GtkWindow *parent, GStrv files_to_share);
