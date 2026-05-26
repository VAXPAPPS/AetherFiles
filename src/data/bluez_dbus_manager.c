#include "bluez_dbus_manager.h"

struct _AetherBluezDbusManager {
    GObject parent_instance;
    GDBusConnection *system_bus;
    GDBusConnection *session_bus;
};

G_DEFINE_TYPE(AetherBluezDbusManager, aether_bluez_dbus_manager, G_TYPE_OBJECT)

static AetherBluezDbusManager *default_manager = NULL;

static void aether_bluez_dbus_manager_finalize(GObject *object) {
    AetherBluezDbusManager *self = AETHER_BLUEZ_DBUS_MANAGER(object);
    g_clear_object(&self->system_bus);
    g_clear_object(&self->session_bus);
    G_OBJECT_CLASS(aether_bluez_dbus_manager_parent_class)->finalize(object);
}

static void aether_bluez_dbus_manager_class_init(AetherBluezDbusManagerClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = aether_bluez_dbus_manager_finalize;
}

static void aether_bluez_dbus_manager_init(AetherBluezDbusManager *self) {
    GError *error = NULL;
    self->system_bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (error) {
        g_warning("Failed to connect to system bus: %s", error->message);
        g_error_free(error);
        error = NULL;
    }

    self->session_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (error) {
        g_warning("Failed to connect to session bus: %s", error->message);
        g_error_free(error);
    }
}

AetherBluezDbusManager *aether_bluez_dbus_manager_get_default(void) {
    if (!default_manager) {
        default_manager = g_object_new(AETHER_TYPE_BLUEZ_DBUS_MANAGER, NULL);
        g_object_add_weak_pointer(G_OBJECT(default_manager), (gpointer *)&default_manager);
    }
    return default_manager;
}

GList *aether_bluez_dbus_manager_get_devices(AetherBluezDbusManager *self) {
    if (!self->system_bus) return NULL;

    GList *device_list = NULL;
    GError *error = NULL;

    /* Get Managed Objects from BlueZ */
    GVariant *result = g_dbus_connection_call_sync(
        self->system_bus,
        "org.bluez",
        "/",
        "org.freedesktop.DBus.ObjectManager",
        "GetManagedObjects",
        NULL,
        G_VARIANT_TYPE("(a{oa{sa{sv}}})"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error
    );

    if (error) {
        g_warning("Failed to get BlueZ managed objects: %s", error->message);
        g_error_free(error);
        return NULL;
    }

    GVariant *objects = g_variant_get_child_value(result, 0);
    GVariantIter iter;
    const char *object_path;
    GVariant *interfaces;

    g_variant_iter_init(&iter, objects);
    while (g_variant_iter_next(&iter, "{&o@a{sa{sv}}}", &object_path, &interfaces)) {
        GVariant *device_props = g_variant_lookup_value(interfaces, "org.bluez.Device1", G_VARIANT_TYPE("a{sv}"));
        if (device_props) {
            const char *address = NULL;
            const char *name = NULL;
            gboolean paired = FALSE;
            gboolean trusted = FALSE;

            g_variant_lookup(device_props, "Address", "&s", &address);
            g_variant_lookup(device_props, "Name", "&s", &name);
            g_variant_lookup(device_props, "Paired", "b", &paired);
            g_variant_lookup(device_props, "Trusted", "b", &trusted);

            if (address) {
                AetherBluetoothDeviceEntity *device = aether_bluetooth_device_entity_new(
                    name ? name : "Unknown Device",
                    address,
                    object_path,
                    paired,
                    trusted
                );
                device_list = g_list_append(device_list, device);
            }
            g_variant_unref(device_props);
        }
        g_variant_unref(interfaces);
    }

    g_variant_unref(objects);
    g_variant_unref(result);

    return device_list;
}

typedef struct {
    char *device_address;
    char *file_path;
    AetherBluezDbusManager *manager;
} ObexTransferData;

static void obex_transfer_data_free(ObexTransferData *data) {
    g_free(data->device_address);
    g_free(data->file_path);
    g_clear_object(&data->manager);
    g_free(data);
}

static void on_obex_send_file_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GTask *task = G_TASK(user_data);
    GError *error = NULL;
    
    GVariant *result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source_object), res, &error);
    if (error) {
        g_task_return_error(task, error);
    } else {
        g_variant_unref(result);
        g_task_return_boolean(task, TRUE);
    }
    g_object_unref(task);
}

static void on_obex_session_ready(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    GTask *task = G_TASK(user_data);
    ObexTransferData *transfer = g_task_get_task_data(task);
    GError *error = NULL;

    GVariant *result = g_dbus_connection_call_finish(G_DBUS_CONNECTION(source_object), res, &error);
    if (error) {
        g_task_return_error(task, error);
        g_object_unref(task);
        return;
    }

    const char *session_path = NULL;
    g_variant_get(result, "(&o)", &session_path);

    /* Now we have the session, let's call SendFile on it */
    g_dbus_connection_call(
        transfer->manager->session_bus,
        "org.bluez.obex",
        session_path,
        "org.bluez.obex.ObjectPush1",
        "SendFile",
        g_variant_new("(s)", transfer->file_path),
        G_VARIANT_TYPE("(oa{sv})"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        on_obex_send_file_ready,
        task
    );

    g_variant_unref(result);
}

void aether_bluez_dbus_manager_send_file_async(AetherBluezDbusManager *self, const char *device_address, const char *file_path, GAsyncReadyCallback callback, gpointer user_data) {
    GTask *task = g_task_new(self, NULL, callback, user_data);
    
    if (!self->session_bus) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "No DBus session bus available");
        g_object_unref(task);
        return;
    }

    ObexTransferData *transfer = g_new0(ObexTransferData, 1);
    transfer->device_address = g_strdup(device_address);
    transfer->file_path = g_strdup(file_path);
    transfer->manager = g_object_ref(self);
    g_task_set_task_data(task, transfer, (GDestroyNotify)obex_transfer_data_free);

    /* Create OBEX Session */
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&builder, "{sv}", "Target", g_variant_new_string("opp"));
    
    g_dbus_connection_call(
        self->session_bus,
        "org.bluez.obex",
        "/org/bluez/obex",
        "org.bluez.obex.Client1",
        "CreateSession",
        g_variant_new("(sa{sv})", device_address, &builder),
        G_VARIANT_TYPE("(o)"),
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        on_obex_session_ready,
        task
    );
}

gboolean aether_bluez_dbus_manager_send_file_finish(AetherBluezDbusManager *self, GAsyncResult *res, GError **error) {
    return g_task_propagate_boolean(G_TASK(res), error);
}
