#include "app_entity.h"

struct _AetherAppEntity {
    GObject parent_instance;

    char *name;
    char *exec;
    char *icon_name;
    char *desktop_path;
    char *categories;
};

G_DEFINE_TYPE(AetherAppEntity, aether_app_entity, G_TYPE_OBJECT)

static void aether_app_entity_finalize(GObject *object) {
    AetherAppEntity *self = AETHER_APP_ENTITY(object);
    g_free(self->name);
    g_free(self->exec);
    g_free(self->icon_name);
    g_free(self->desktop_path);
    g_free(self->categories);
    G_OBJECT_CLASS(aether_app_entity_parent_class)->finalize(object);
}

static void aether_app_entity_class_init(AetherAppEntityClass *klass) {
    GObjectClass *oclass = G_OBJECT_CLASS(klass);
    oclass->finalize = aether_app_entity_finalize;
}

static void aether_app_entity_init(AetherAppEntity *self) {
    self->name = NULL;
    self->exec = NULL;
    self->icon_name = NULL;
    self->desktop_path = NULL;
    self->categories = NULL;
}

AetherAppEntity *aether_app_entity_new(const char *name, const char *exec, const char *icon_name, const char *desktop_path, const char *categories) {
    AetherAppEntity *self = g_object_new(AETHER_TYPE_APP_ENTITY, NULL);
    self->name = g_strdup(name);
    self->exec = g_strdup(exec);
    self->icon_name = g_strdup(icon_name);
    self->desktop_path = g_strdup(desktop_path);
    self->categories = g_strdup(categories);
    return self;
}

const char *aether_app_entity_get_name(AetherAppEntity *self) { return self->name; }
const char *aether_app_entity_get_exec(AetherAppEntity *self) { return self->exec; }
const char *aether_app_entity_get_icon_name(AetherAppEntity *self) { return self->icon_name; }
const char *aether_app_entity_get_desktop_path(AetherAppEntity *self) { return self->desktop_path; }
const char *aether_app_entity_get_categories(AetherAppEntity *self) { return self->categories; }
