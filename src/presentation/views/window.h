#pragma once
#include <gtk/gtk.h>
#include "../application.h"

#define AETHER_TYPE_WINDOW (aether_window_get_type())
G_DECLARE_FINAL_TYPE(AetherWindow, aether_window, AETHER, WINDOW, GtkApplicationWindow)

GtkWindow   *aether_window_new              (AetherApplication *app);
const char  *aether_window_get_current_path (AetherWindow *self);
void         aether_window_reload           (AetherWindow *self);
GStrv        aether_window_get_selected_paths(AetherWindow *self);
void         aether_window_start_progress   (AetherWindow *self);
void         aether_window_stop_progress    (AetherWindow *self);
gboolean     aether_window_get_elevated_mode(AetherWindow *self);
