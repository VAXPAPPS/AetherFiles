#pragma once
#include <glib-object.h>

#define AETHER_TYPE_FILE_ENTITY (aether_file_entity_get_type())
G_DECLARE_FINAL_TYPE(AetherFileEntity, aether_file_entity, AETHER, FILE_ENTITY, GObject)

AetherFileEntity *aether_file_entity_new(const char *name, const char *path, const char *uri, goffset size, gboolean is_directory, const char *icon_name);

const char *aether_file_entity_get_name(AetherFileEntity *self);
const char *aether_file_entity_get_path(AetherFileEntity *self);
const char *aether_file_entity_get_uri(AetherFileEntity *self);
goffset     aether_file_entity_get_size(AetherFileEntity *self);
gboolean    aether_file_entity_is_directory(AetherFileEntity *self);
const char *aether_file_entity_get_icon_name(AetherFileEntity *self);
