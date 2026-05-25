#include "app_repository.h"
#include "../domain/app_entity.h"

#include <glib/gstdio.h>
#include <string.h>

struct _AetherAppRepository {
    GObject parent_instance;
    GListStore *apps_store;
};

G_DEFINE_TYPE(AetherAppRepository, aether_app_repository, G_TYPE_OBJECT)

static void aether_app_repository_dispose(GObject *object) {
    AetherAppRepository *self = AETHER_APP_REPOSITORY(object);
    g_clear_object(&self->apps_store);
    G_OBJECT_CLASS(aether_app_repository_parent_class)->dispose(object);
}

static void aether_app_repository_class_init(AetherAppRepositoryClass *klass) {
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    oclass->dispose = aether_app_repository_dispose;
}

static void aether_app_repository_init(AetherAppRepository *self) {
    self->apps_store = g_list_store_new(AETHER_TYPE_APP_ENTITY);
}

AetherAppRepository *aether_app_repository_new(void) {
    return g_object_new(AETHER_TYPE_APP_REPOSITORY, NULL);
}

GListStore *aether_app_repository_get_apps(AetherAppRepository *self) {
    return self->apps_store;
}

/* Helper to strip %u, %f, etc. from Exec lines */
static char *clean_exec(const char *exec) {
    if (!exec) return NULL;
    char **parts = g_strsplit(exec, " ", -1);
    GString *out = g_string_new("");
    for (int i = 0; parts[i] != NULL; i++) {
        if (parts[i][0] == '%' && strlen(parts[i]) == 2) {
            continue;
        }
        if (out->len > 0) g_string_append_c(out, ' ');
        g_string_append(out, parts[i]);
    }
    g_strfreev(parts);
    return g_string_free(out, FALSE);
}

static void parse_desktop_file(AetherAppRepository *self, const char *path) {
    GKeyFile *kf = g_key_file_new();
    GError *err = NULL;

    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &err)) {
        g_error_free(err);
        g_key_file_free(kf);
        return;
    }

    char *type = g_key_file_get_string(kf, "Desktop Entry", "Type", NULL);
    if (!type || g_strcmp0(type, "Application") != 0) {
        g_free(type);
        g_key_file_free(kf);
        return;
    }
    g_free(type);

    gboolean no_display = g_key_file_get_boolean(kf, "Desktop Entry", "NoDisplay", NULL);
    if (no_display) {
        g_key_file_free(kf);
        return;
    }

    char *name = g_key_file_get_locale_string(kf, "Desktop Entry", "Name", NULL, NULL);
    if (!name) name = g_key_file_get_string(kf, "Desktop Entry", "Name", NULL);

    char *exec_raw = g_key_file_get_string(kf, "Desktop Entry", "Exec", NULL);
    char *exec = clean_exec(exec_raw);
    g_free(exec_raw);

    char *icon_name = g_key_file_get_string(kf, "Desktop Entry", "Icon", NULL);
    char *categories = g_key_file_get_string(kf, "Desktop Entry", "Categories", NULL);

    if (name && exec) {
        AetherAppEntity *entity = aether_app_entity_new(name, exec, icon_name, path, categories);
        g_list_store_append(self->apps_store, entity);
        g_object_unref(entity);
    }

    g_free(name);
    g_free(exec);
    g_free(icon_name);
    g_free(categories);
    g_key_file_free(kf);
}

static void scan_directory(AetherAppRepository *self, const char *dir_path) {
    GDir *dir = g_dir_open(dir_path, 0, NULL);
    if (!dir) return;

    const char *filename;
    while ((filename = g_dir_read_name(dir))) {
        if (!g_str_has_suffix(filename, ".desktop")) continue;
        char *full_path = g_build_filename(dir_path, filename, NULL);
        parse_desktop_file(self, full_path);
        g_free(full_path);
    }
    g_dir_close(dir);
}

static int app_entity_compare(gconstpointer a, gconstpointer b, gpointer user_data) {
    (void)user_data;
    AetherAppEntity *ea = AETHER_APP_ENTITY((gpointer)a);
    AetherAppEntity *eb = AETHER_APP_ENTITY((gpointer)b);
    
    const char *na = aether_app_entity_get_name(ea);
    const char *nb = aether_app_entity_get_name(eb);

    if (!na) return 1;
    if (!nb) return -1;
    return g_utf8_collate(na, nb);
}

void aether_app_repository_load_apps(AetherAppRepository *self) {
    g_list_store_remove_all(self->apps_store);

    scan_directory(self, "/usr/share/applications");
    scan_directory(self, "/usr/local/share/applications");

    char *user_dir = g_build_filename(g_get_home_dir(), ".local", "share", "applications", NULL);
    scan_directory(self, user_dir);
    g_free(user_dir);

    g_list_store_sort(self->apps_store, app_entity_compare, NULL);
}
