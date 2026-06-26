#ifndef EXTERNAL_DROP_CONTROLLER_H
#define EXTERNAL_DROP_CONTROLLER_H

#include <gtk/gtk.h>
#include "../views/window.h"

G_BEGIN_DECLS

/**
 * Handles complex drop operations from external sources (browser URLs, text, text/uri-list).
 * Automatically separates local files from web links, prompts for downloads,
 * and handles snippet creation.
 * 
 * Returns TRUE if the drop was consumed by this handler.
 */
gboolean aether_external_drop_handle(AetherWindow *win, const GValue *value);

G_END_DECLS

#endif /* EXTERNAL_DROP_CONTROLLER_H */
