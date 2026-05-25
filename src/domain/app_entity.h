#ifndef AETHER_APP_ENTITY_H
#define AETHER_APP_ENTITY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define AETHER_TYPE_APP_ENTITY (aether_app_entity_get_type())
G_DECLARE_FINAL_TYPE(AetherAppEntity, aether_app_entity, AETHER, APP_ENTITY, GObject)

AetherAppEntity *aether_app_entity_new(const char *name, const char *exec, const char *icon_name, const char *desktop_path, const char *categories);

const char *aether_app_entity_get_name(AetherAppEntity *self);
const char *aether_app_entity_get_exec(AetherAppEntity *self);
const char *aether_app_entity_get_icon_name(AetherAppEntity *self);
const char *aether_app_entity_get_desktop_path(AetherAppEntity *self);
const char *aether_app_entity_get_categories(AetherAppEntity *self);

G_END_DECLS

#endif /* AETHER_APP_ENTITY_H */
