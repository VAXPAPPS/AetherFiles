#include "file_entity.h"

struct _AetherFileEntity {
    GObject parent_instance;
    char *name;
    char *path;
    char *uri;
    goffset size;
    gboolean is_directory;
    char *icon_name;
    GdkTexture *thumbnail;
    gboolean is_loading_thumbnail;
};

G_DEFINE_TYPE(AetherFileEntity, aether_file_entity, G_TYPE_OBJECT)

enum {
    SIGNAL_THUMBNAIL_UPDATED,
    LAST_SIGNAL
};
static guint file_entity_signals[LAST_SIGNAL] = { 0 };

static void aether_file_entity_finalize(GObject *object) {
    AetherFileEntity *self = AETHER_FILE_ENTITY(object);
    g_free(self->name);
    g_free(self->path);
    g_free(self->uri);
    g_free(self->icon_name);
    g_clear_object(&self->thumbnail);
    G_OBJECT_CLASS(aether_file_entity_parent_class)->finalize(object);
}

static void aether_file_entity_class_init(AetherFileEntityClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = aether_file_entity_finalize;

    file_entity_signals[SIGNAL_THUMBNAIL_UPDATED] = g_signal_new(
        "thumbnail-updated",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0, NULL, NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0
    );
}

static void aether_file_entity_init(AetherFileEntity *self) {
}

AetherFileEntity *aether_file_entity_new(const char *name, const char *path, const char *uri, goffset size, gboolean is_directory, const char *icon_name) {
    AetherFileEntity *self = g_object_new(AETHER_TYPE_FILE_ENTITY, NULL);
    self->name = g_strdup(name);
    self->path = g_strdup(path);
    self->uri = g_strdup(uri);
    self->size = size;
    self->is_directory = is_directory;
    self->icon_name = g_strdup(icon_name);
    return self;
}

const char *aether_file_entity_get_name(AetherFileEntity *self) { return self->name; }
const char *aether_file_entity_get_path(AetherFileEntity *self) { return self->path; }
const char *aether_file_entity_get_uri(AetherFileEntity *self)  { return self->uri; }
goffset     aether_file_entity_get_size(AetherFileEntity *self)  { return self->size; }
gboolean    aether_file_entity_is_directory(AetherFileEntity *self) { return self->is_directory; }
const char *aether_file_entity_get_icon_name(AetherFileEntity *self) { return self->icon_name; }

GdkTexture *aether_file_entity_get_thumbnail(AetherFileEntity *self) {
    return self->thumbnail;
}

void aether_file_entity_set_thumbnail(AetherFileEntity *self, GdkTexture *texture) {
    if (self->thumbnail != texture) {
        g_clear_object(&self->thumbnail);
        if (texture) {
            self->thumbnail = g_object_ref(texture);
        }
        g_signal_emit(self, file_entity_signals[SIGNAL_THUMBNAIL_UPDATED], 0);
    }
}

gboolean aether_file_entity_get_is_loading_thumbnail(AetherFileEntity *self) {
    return self->is_loading_thumbnail;
}

void aether_file_entity_set_is_loading_thumbnail(AetherFileEntity *self, gboolean loading) {
    self->is_loading_thumbnail = loading;
}
