#pragma once

#include <gtk/gtk.h>
#include "../core/app_entry.h"

G_BEGIN_DECLS

#define VENOM_TYPE_UNINSTALL_DIALOG (venom_uninstall_dialog_get_type ())
G_DECLARE_FINAL_TYPE (VenomUninstallDialog, venom_uninstall_dialog,
                      VENOM, UNINSTALL_DIALOG, GtkBox)

/**
 * VenomUninstallDialog - In-app overlay widget for confirming app removal.
 *
 * Rendered as an overlay inside the launcher's GtkOverlay so it always
 * appears on top of the layer-shell surface (not as a separate GtkWindow
 * which would be hidden behind the launcher on Wayland).
 *
 * Signals:
 *  "uninstall-done" (gboolean success) — emitted on main thread when the
 *                                         removal process finishes.
 *  "dismiss"                            — emitted when the user cancels.
 */

GtkWidget *venom_uninstall_dialog_new       (AppEntry *entry);
void       venom_uninstall_dialog_run_async (VenomUninstallDialog *self);

G_END_DECLS
