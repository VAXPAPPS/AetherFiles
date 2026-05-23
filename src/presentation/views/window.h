#pragma once
#include <gtk/gtk.h>
#include <adwaita.h>
#include "../application.h"

#define AETHER_TYPE_WINDOW (aether_window_get_type())
G_DECLARE_FINAL_TYPE(AetherWindow, aether_window, AETHER, WINDOW, AdwApplicationWindow)

GtkWindow *aether_window_new(AetherApplication *app);
