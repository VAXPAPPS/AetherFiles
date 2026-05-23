#include "application.h"
#include "views/window.h"

struct _AetherApplication {
    AdwApplication parent_instance;
};

G_DEFINE_TYPE(AetherApplication, aether_application, ADW_TYPE_APPLICATION)

static void aether_application_activate(GApplication *app) {
    GtkWindow *window = gtk_application_get_active_window(GTK_APPLICATION(app));
    
    if (!window) {
        window = aether_window_new(AETHER_APPLICATION(app));
    }
    
    gtk_window_present(window);
}

static void aether_application_startup(GApplication *app) {
    G_APPLICATION_CLASS(aether_application_parent_class)->startup(app);
    adw_style_manager_set_color_scheme(adw_style_manager_get_default(), ADW_COLOR_SCHEME_PREFER_DARK);
}

static void aether_application_class_init(AetherApplicationClass *klass) {
    GApplicationClass *app_class = G_APPLICATION_CLASS(klass);
    app_class->activate = aether_application_activate;
    app_class->startup = aether_application_startup;
}

static void on_open_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    const char *path = g_variant_get_string(parameter, NULL);
    g_print("Action: Open %s\n", path);
    // Here we can use g_app_info_launch_default_for_uri
    char *uri = g_filename_to_uri(path, NULL, NULL);
    if (uri) {
        g_app_info_launch_default_for_uri(uri, NULL, NULL);
        g_free(uri);
    }
}

static void on_set_background_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    const char *path = g_variant_get_string(parameter, NULL);
    g_print("Action: Set Background to %s\n", path);
    char *cmd = g_strdup_printf("vaxp-setbg \"%s\"", path);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}

static void on_placeholder_action(GSimpleAction *action, GVariant *parameter, gpointer user_data) {
    const char *path = g_variant_get_string(parameter, NULL);
    const char *action_name = g_action_get_name(G_ACTION(action));
    g_print("Action Triggered: %s on %s\n", action_name, path);
}

static void aether_application_init(AetherApplication *app) {
    const char *action_names[] = {
        "open", "set_background", "cut", "copy", "paste", "rename", "trash", "properties", NULL
    };
    
    for (int i = 0; action_names[i] != NULL; i++) {
        GSimpleAction *action = g_simple_action_new(action_names[i], G_VARIANT_TYPE_STRING);
        if (g_strcmp0(action_names[i], "open") == 0) {
            g_signal_connect(action, "activate", G_CALLBACK(on_open_action), app);
        } else if (g_strcmp0(action_names[i], "set_background") == 0) {
            g_signal_connect(action, "activate", G_CALLBACK(on_set_background_action), app);
        } else {
            g_signal_connect(action, "activate", G_CALLBACK(on_placeholder_action), app);
        }
        g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(action));
    }
}

AetherApplication *aether_application_new(void) {
    return g_object_new(AETHER_TYPE_APPLICATION,
                        "application-id", "com.aetheros.files",
                        "flags", G_APPLICATION_DEFAULT_FLAGS,
                        NULL);
}
