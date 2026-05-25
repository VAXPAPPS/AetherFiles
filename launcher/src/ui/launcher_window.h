#pragma once

#include <gtk/gtk.h>
#include <glib.h>

G_BEGIN_DECLS

#define VENOM_TYPE_LAUNCHER_WINDOW (venom_launcher_window_get_type ())
G_DECLARE_FINAL_TYPE (VenomLauncherWindow, venom_launcher_window,
                      VENOM, LAUNCHER_WINDOW, GtkApplicationWindow)

/**
 * VenomLauncherWindow - Fullscreen, translucent launcher window.
 * Owns the app list, search bar, and icon grid.
 */
GtkWidget *venom_launcher_window_new          (GtkApplication *app);
void       venom_launcher_window_show_launcher (VenomLauncherWindow *win);

/**
 * Overlay helpers — add / remove a widget as a centered overlay
 * on top of the launcher contents (inside the same layer-shell surface).
 */
void       venom_launcher_window_push_overlay (VenomLauncherWindow *win,
                                               GtkWidget           *widget);
void       venom_launcher_window_pop_overlay  (VenomLauncherWindow *win,
                                               GtkWidget           *widget);

G_END_DECLS
