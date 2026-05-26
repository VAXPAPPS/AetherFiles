#include "bluetooth_share_dialog.h"
#include "../../domain/bluetooth_device_entity.h"

struct _AetherBluetoothShareDialog {
    GtkWindow parent_instance;
    GStrv files_to_share;
    AetherBluezDbusManager *dbus_manager;

    GtkWidget *list_box;
    GtkWidget *status_label;
    GtkWidget *spinner;
};

G_DEFINE_TYPE(AetherBluetoothShareDialog, aether_bluetooth_share_dialog, GTK_TYPE_WINDOW)

static void aether_bluetooth_share_dialog_finalize(GObject *object) {
    AetherBluetoothShareDialog *self = AETHER_BLUETOOTH_SHARE_DIALOG(object);
    g_strfreev(self->files_to_share);
    g_clear_object(&self->dbus_manager);
    G_OBJECT_CLASS(aether_bluetooth_share_dialog_parent_class)->finalize(object);
}

static int pending_transfers = 0;

static void on_file_sent(GObject *source, GAsyncResult *res, gpointer user_data) {
    AetherBluetoothShareDialog *self = AETHER_BLUETOOTH_SHARE_DIALOG(user_data);
    GError *error = NULL;
    gboolean success = aether_bluez_dbus_manager_send_file_finish(AETHER_BLUEZ_DBUS_MANAGER(source), res, &error);
    
    if (!success) {
        g_printerr("Failed to send file over Bluetooth: %s\n", error->message);
        g_error_free(error);
    }
    
    pending_transfers--;
    if (pending_transfers <= 0) {
        gtk_window_destroy(GTK_WINDOW(self));
    }
}

static void on_device_selected(GtkListBox *box, GtkListBoxRow *row, AetherBluetoothShareDialog *self) {
    if (!row) return;

    AetherBluetoothDeviceEntity *device = g_object_get_data(G_OBJECT(row), "device");
    if (!device) return;

    const char *address = aether_bluetooth_device_entity_get_address(device);
    const char *name = aether_bluetooth_device_entity_get_name(device);

    char *msg = g_strdup_printf("Sending to %s...", name);
    gtk_label_set_text(GTK_LABEL(self->status_label), msg);
    g_free(msg);

    gtk_widget_set_visible(self->spinner, TRUE);

    if (self->files_to_share) {
        for (int i = 0; self->files_to_share[i] != NULL; i++) {
            pending_transfers++;
            aether_bluez_dbus_manager_send_file_async(self->dbus_manager, address, self->files_to_share[i], on_file_sent, self);
        }
    }
    
    if (pending_transfers == 0) {
        gtk_window_destroy(GTK_WINDOW(self));
    }
}

static GtkWidget *create_device_row(AetherBluetoothDeviceEntity *device) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_start(box, 12);
    gtk_widget_set_margin_end(box, 12);
    gtk_widget_set_margin_top(box, 12);
    gtk_widget_set_margin_bottom(box, 12);

    GtkWidget *icon = gtk_image_new_from_icon_name("bluetooth-active-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 24);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *name_label = gtk_label_new(aether_bluetooth_device_entity_get_name(device));
    gtk_widget_set_halign(name_label, GTK_ALIGN_START);
    
    char *desc = g_strdup_printf("%s %s", 
        aether_bluetooth_device_entity_get_address(device),
        aether_bluetooth_device_entity_get_paired(device) ? "(Paired)" : "");
    GtkWidget *desc_label = gtk_label_new(desc);
    gtk_widget_add_css_class(desc_label, "dim-label");
    gtk_widget_set_halign(desc_label, GTK_ALIGN_START);
    g_free(desc);

    gtk_box_append(GTK_BOX(vbox), name_label);
    gtk_box_append(GTK_BOX(vbox), desc_label);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), vbox);

    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data_full(G_OBJECT(row), "device", g_object_ref(device), g_object_unref);

    return row;
}

static void populate_devices(AetherBluetoothShareDialog *self) {
    GList *devices = aether_bluez_dbus_manager_get_devices(self->dbus_manager);
    
    if (!devices) {
        gtk_label_set_text(GTK_LABEL(self->status_label), "No Bluetooth devices found. Make sure Bluetooth is enabled.");
        gtk_widget_set_visible(self->spinner, FALSE);
        return;
    }

    for (GList *l = devices; l != NULL; l = l->next) {
        AetherBluetoothDeviceEntity *device = AETHER_BLUETOOTH_DEVICE_ENTITY(l->data);
        GtkWidget *row = create_device_row(device);
        gtk_list_box_append(GTK_LIST_BOX(self->list_box), row);
    }
    g_list_free_full(devices, g_object_unref);

    gtk_label_set_text(GTK_LABEL(self->status_label), "Select a device to share files");
    gtk_widget_set_visible(self->spinner, FALSE);
}

static void aether_bluetooth_share_dialog_class_init(AetherBluetoothShareDialogClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = aether_bluetooth_share_dialog_finalize;
}

static void aether_bluetooth_share_dialog_init(AetherBluetoothShareDialog *self) {
    gtk_window_set_title(GTK_WINDOW(self), "Share via Bluetooth");
    gtk_window_set_default_size(GTK_WINDOW(self), 400, 500);
    gtk_window_set_modal(GTK_WINDOW(self), TRUE);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    GtkWidget *header = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(header, 24);
    gtk_widget_set_margin_bottom(header, 24);
    
    self->spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(self->spinner));
    gtk_widget_set_halign(self->spinner, GTK_ALIGN_CENTER);
    
    self->status_label = gtk_label_new("Scanning for devices...");
    gtk_widget_set_halign(self->status_label, GTK_ALIGN_CENTER);

    gtk_box_append(GTK_BOX(header), self->spinner);
    gtk_box_append(GTK_BOX(header), self->status_label);

    self->list_box = gtk_list_box_new();
    gtk_widget_set_margin_start(self->list_box, 12);
    gtk_widget_set_margin_end(self->list_box, 12);
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->list_box), GTK_SELECTION_SINGLE);
    g_signal_connect(self->list_box, "row-activated", G_CALLBACK(on_device_selected), self);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), self->list_box);

    gtk_box_append(GTK_BOX(vbox), header);
    gtk_box_append(GTK_BOX(vbox), scroll);

    gtk_window_set_child(GTK_WINDOW(self), vbox);
}

AetherBluetoothShareDialog *aether_bluetooth_share_dialog_new(GtkWindow *parent, GStrv files_to_share) {
    AetherBluetoothShareDialog *self = g_object_new(AETHER_TYPE_BLUETOOTH_SHARE_DIALOG, "transient-for", parent, NULL);
    
    self->files_to_share = g_strdupv(files_to_share);

    self->dbus_manager = aether_bluez_dbus_manager_get_default();
    g_object_ref(self->dbus_manager);

    populate_devices(self);

    return self;
}
