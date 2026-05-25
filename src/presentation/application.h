#pragma once
#include <gtk/gtk.h>

#define AETHER_TYPE_APPLICATION (aether_application_get_type())
G_DECLARE_FINAL_TYPE(AetherApplication, aether_application, AETHER, APPLICATION, GtkApplication)

AetherApplication *aether_application_new(void);
