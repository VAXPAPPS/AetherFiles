#ifndef AETHER_APP_UNINSTALL_DIALOG_H
#define AETHER_APP_UNINSTALL_DIALOG_H

#include <gtk/gtk.h>
#include "../../domain/app_entity.h"

G_BEGIN_DECLS

#define AETHER_TYPE_APP_UNINSTALL_DIALOG (aether_app_uninstall_dialog_get_type())
G_DECLARE_FINAL_TYPE(AetherAppUninstallDialog, aether_app_uninstall_dialog, AETHER, APP_UNINSTALL_DIALOG, GtkWindow)

GtkWidget *aether_app_uninstall_dialog_new(GtkWindow *parent, AetherAppEntity *entry);
void aether_app_uninstall_dialog_run_async(AetherAppUninstallDialog *self);

G_END_DECLS

#endif /* AETHER_APP_UNINSTALL_DIALOG_H */
