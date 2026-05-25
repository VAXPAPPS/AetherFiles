#ifndef AETHER_APP_REPOSITORY_H
#define AETHER_APP_REPOSITORY_H

#include <glib-object.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define AETHER_TYPE_APP_REPOSITORY (aether_app_repository_get_type())
G_DECLARE_FINAL_TYPE(AetherAppRepository, aether_app_repository, AETHER, APP_REPOSITORY, GObject)

AetherAppRepository *aether_app_repository_new(void);

GListStore *aether_app_repository_get_apps(AetherAppRepository *self);

void aether_app_repository_load_apps(AetherAppRepository *self);

G_END_DECLS

#endif /* AETHER_APP_REPOSITORY_H */
