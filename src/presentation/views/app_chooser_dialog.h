#pragma once

#include <gtk/gtk.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define AETHER_TYPE_APP_CHOOSER_DIALOG (aether_app_chooser_dialog_get_type())
G_DECLARE_FINAL_TYPE(AetherAppChooserDialog, aether_app_chooser_dialog, AETHER, APP_CHOOSER_DIALOG, GtkWindow)

AetherAppChooserDialog *aether_app_chooser_dialog_new(GtkWindow *parent, GFile *file);

G_END_DECLS
