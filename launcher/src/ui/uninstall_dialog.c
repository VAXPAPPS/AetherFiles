#include "uninstall_dialog.h"
#include "launcher_window.h"
#include "../core/icon_loader.h"

#include <glib/gspawn.h>
#include <glib/gstdio.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Signals
 * ------------------------------------------------------------------------- */

enum {
    SIGNAL_UNINSTALL_DONE,
    SIGNAL_DISMISS,
    N_SIGNALS
};
static guint signals[N_SIGNALS];

/* -------------------------------------------------------------------------
 * Widget struct
 *
 * The widget is a full-screen GtkBox (backdrop) that contains a centered
 * "card" GtkBox.  It is added to the launcher's GtkOverlay so it lives
 * inside the same Wayland surface.
 * ------------------------------------------------------------------------- */

struct _VenomUninstallDialog {
    GtkBox      parent_instance;    /* full-screen backdrop */

    AppEntry   *entry;              /* Not owned */

    /* Inner card stack */
    GtkWidget  *card;               /* styled card container */
    GtkWidget  *stack;
    GtkWidget  *page_confirm;
    GtkWidget  *page_progress;
    GtkWidget  *page_result;

    /* Widgets updated at runtime */
    GtkWidget  *spinner;
    GtkWidget  *status_label;
    GtkWidget  *result_icon;
    GtkWidget  *result_label;
    GtkWidget  *result_detail;
};

G_DEFINE_TYPE (VenomUninstallDialog, venom_uninstall_dialog, GTK_TYPE_BOX)

/* -------------------------------------------------------------------------
 * CSS
 * ------------------------------------------------------------------------- */

static void
inject_dialog_css (void)
{
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;

    static const char *css =
        /* Full-screen backdrop (dimmer) */
        ".uninstall-backdrop {"
        "  background: rgba(0,0,0,0.55);"
        "}"
        /* Floating card */
        ".uninstall-card {"
        "  background: rgba(18,20,28,0.96);"
        "  border-radius: 18px;"
        "  border: 1px solid rgba(255,255,255,0.10);"
        "  box-shadow: 0 24px 64px rgba(0,0,0,0.7);"
        "}"
        /* App name */
        ".uninstall-app-name {"
        "  font-size: 17px;"
        "  font-weight: bold;"
        "  color: rgba(255,255,255,0.92);"
        "}"
        /* Warning text */
        ".uninstall-warn {"
        "  font-size: 13px;"
        "  color: rgba(255,255,255,0.55);"
        "}"
        /* Confirm (red) button */
        ".uninstall-confirm-btn {"
        "  background: rgba(210,45,45,0.88);"
        "  color: white;"
        "  border-radius: 9px;"
        "  padding: 7px 20px;"
        "  border: none;"
        "  font-weight: 600;"
        "  font-size: 13px;"
        "}"
        ".uninstall-confirm-btn:hover {"
        "  background: rgba(190,28,28,0.98);"
        "}"
        /* Cancel button */
        ".uninstall-cancel-btn {"
        "  background: rgba(255,255,255,0.08);"
        "  color: rgba(255,255,255,0.72);"
        "  border-radius: 9px;"
        "  padding: 7px 18px;"
        "  border: none;"
        "  font-size: 13px;"
        "}"
        ".uninstall-cancel-btn:hover {"
        "  background: rgba(255,255,255,0.15);"
        "}"
        /* Spinner label */
        ".uninstall-status {"
        "  color: rgba(255,255,255,0.65);"
        "  font-size: 13px;"
        "}"
        /* Result title */
        ".uninstall-result-title {"
        "  font-size: 16px;"
        "  font-weight: bold;"
        "  color: rgba(255,255,255,0.88);"
        "}"
        /* Result detail */
        ".uninstall-result-detail {"
        "  font-size: 12px;"
        "  color: rgba(255,255,255,0.50);"
        "}"
        ".result-success { color: #4cda8a; }"
        ".result-error   { color: #e05555; }";

    GtkCssProvider *prov = gtk_css_provider_new ();
    gtk_css_provider_load_from_data (prov, css, -1, NULL);
    gtk_style_context_add_provider_for_screen (
        gdk_screen_get_default (),
        GTK_STYLE_PROVIDER (prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 5);
    g_object_unref (prov);
}

/* -------------------------------------------------------------------------
 * Dismiss helper
 * ------------------------------------------------------------------------- */

static void
dismiss_self (VenomUninstallDialog *self)
{
    /* Find the launcher window and ask it to remove this overlay */
    GtkWidget *overlay_parent = gtk_widget_get_parent (GTK_WIDGET (self));
    if (overlay_parent && GTK_IS_OVERLAY (overlay_parent)) {
        GtkWidget *launcher = gtk_widget_get_parent (overlay_parent);
        if (launcher && VENOM_IS_LAUNCHER_WINDOW (launcher)) {
            venom_launcher_window_pop_overlay (
                VENOM_LAUNCHER_WINDOW (launcher), GTK_WIDGET (self));
            return;
        }
    }
    /* Fallback */
    gtk_widget_destroy (GTK_WIDGET (self));
}

/* -------------------------------------------------------------------------
 * Page builders
 * ------------------------------------------------------------------------- */

static void
on_cancel_clicked (GtkButton *btn, gpointer data)
{
    (void) btn;
    VenomUninstallDialog *self = VENOM_UNINSTALL_DIALOG (data);
    g_signal_emit (self, signals[SIGNAL_DISMISS], 0);
    dismiss_self (self);
}

static GtkWidget *
build_confirm_page (VenomUninstallDialog *self)
{
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_start  (vbox, 32);
    gtk_widget_set_margin_end    (vbox, 32);
    gtk_widget_set_margin_top    (vbox, 28);
    gtk_widget_set_margin_bottom (vbox, 24);

    /* App icon */
    GtkWidget *icon_img = gtk_image_new_from_icon_name (
        self->entry && self->entry->icon_name
            ? self->entry->icon_name : "application-x-executable",
        GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size (GTK_IMAGE (icon_img), 64);
    gtk_widget_set_halign (icon_img, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (vbox), icon_img, FALSE, FALSE, 0);

    /* App name */
    GtkWidget *name_lbl = gtk_label_new (
        self->entry && self->entry->name ? self->entry->name : "");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (name_lbl), "uninstall-app-name");
    gtk_label_set_xalign (GTK_LABEL (name_lbl), 0.5f);
    gtk_box_pack_start (GTK_BOX (vbox), name_lbl, FALSE, FALSE, 0);

    /* Warning */
    const char *pkg = (self->entry && self->entry->package_name)
                      ? self->entry->package_name : NULL;
    char *warn_text;
    if (pkg) {
        warn_text = g_strdup_printf (
            "سيتم محاولة إزالة الحزمة \"%s\"\n"
            "وحذف ملف الاختصار.\n"
            "هذا الإجراء لا يمكن التراجع عنه.", pkg);
    } else {
        warn_text = g_strdup (
            "سيتم حذف اختصار التطبيق.\n"
            "هذا الإجراء لا يمكن التراجع عنه.");
    }
    GtkWidget *warn = gtk_label_new (warn_text);
    g_free (warn_text);
    gtk_style_context_add_class (gtk_widget_get_style_context (warn), "uninstall-warn");
    gtk_label_set_justify (GTK_LABEL (warn), GTK_JUSTIFY_CENTER);
    gtk_label_set_line_wrap (GTK_LABEL (warn), TRUE);
    gtk_box_pack_start (GTK_BOX (vbox), warn, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (vbox),
                        gtk_separator_new (GTK_ORIENTATION_HORIZONTAL),
                        FALSE, FALSE, 4);

    /* Buttons */
    GtkWidget *btn_box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign (btn_box, GTK_ALIGN_END);
    gtk_box_pack_start (GTK_BOX (vbox), btn_box, FALSE, FALSE, 0);

    GtkWidget *cancel_btn = gtk_button_new_with_label ("إلغاء");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (cancel_btn), "uninstall-cancel-btn");
    g_signal_connect (cancel_btn, "clicked", G_CALLBACK (on_cancel_clicked), self);
    gtk_box_pack_end (GTK_BOX (btn_box), cancel_btn, FALSE, FALSE, 0);

    GtkWidget *confirm_btn = gtk_button_new_with_label ("حذف التطبيق");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (confirm_btn), "uninstall-confirm-btn");
    g_signal_connect_swapped (confirm_btn, "clicked",
                              G_CALLBACK (venom_uninstall_dialog_run_async), self);
    gtk_box_pack_end (GTK_BOX (btn_box), confirm_btn, FALSE, FALSE, 0);

    return vbox;
}

static GtkWidget *
build_progress_page (VenomUninstallDialog *self)
{
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start  (vbox, 40);
    gtk_widget_set_margin_end    (vbox, 40);
    gtk_widget_set_margin_top    (vbox, 36);
    gtk_widget_set_margin_bottom (vbox, 36);
    gtk_widget_set_valign (vbox, GTK_ALIGN_CENTER);

    self->spinner = gtk_spinner_new ();
    gtk_widget_set_halign (self->spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request (self->spinner, 48, 48);
    gtk_box_pack_start (GTK_BOX (vbox), self->spinner, FALSE, FALSE, 0);

    self->status_label = gtk_label_new ("جارٍ إزالة التطبيق\342\200\246");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (self->status_label), "uninstall-status");
    gtk_box_pack_start (GTK_BOX (vbox), self->status_label, FALSE, FALSE, 0);

    return vbox;
}

static GtkWidget *
build_result_page (VenomUninstallDialog *self)
{
    GtkWidget *vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start  (vbox, 32);
    gtk_widget_set_margin_end    (vbox, 32);
    gtk_widget_set_margin_top    (vbox, 28);
    gtk_widget_set_margin_bottom (vbox, 24);
    gtk_widget_set_valign (vbox, GTK_ALIGN_CENTER);

    self->result_icon = gtk_image_new_from_icon_name (
        "dialog-information", GTK_ICON_SIZE_DIALOG);
    gtk_image_set_pixel_size (GTK_IMAGE (self->result_icon), 52);
    gtk_widget_set_halign (self->result_icon, GTK_ALIGN_CENTER);
    gtk_box_pack_start (GTK_BOX (vbox), self->result_icon, FALSE, FALSE, 0);

    self->result_label = gtk_label_new ("");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (self->result_label), "uninstall-result-title");
    gtk_label_set_xalign (GTK_LABEL (self->result_label), 0.5f);
    gtk_box_pack_start (GTK_BOX (vbox), self->result_label, FALSE, FALSE, 0);

    self->result_detail = gtk_label_new ("");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (self->result_detail), "uninstall-result-detail");
    gtk_label_set_line_wrap (GTK_LABEL (self->result_detail), TRUE);
    gtk_label_set_justify (GTK_LABEL (self->result_detail), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars (GTK_LABEL (self->result_detail), 38);
    gtk_box_pack_start (GTK_BOX (vbox), self->result_detail, FALSE, FALSE, 0);

    gtk_box_pack_start (GTK_BOX (vbox),
                        gtk_separator_new (GTK_ORIENTATION_HORIZONTAL),
                        FALSE, FALSE, 4);

    GtkWidget *close_btn = gtk_button_new_with_label ("\330\245\330\272\331\204\330\247\331\202");
    gtk_style_context_add_class (
        gtk_widget_get_style_context (close_btn), "uninstall-cancel-btn");
    gtk_widget_set_halign (close_btn, GTK_ALIGN_END);
    g_signal_connect (close_btn, "clicked", G_CALLBACK (on_cancel_clicked), self);
    gtk_box_pack_start (GTK_BOX (vbox), close_btn, FALSE, FALSE, 0);

    return vbox;
}

/* -------------------------------------------------------------------------
 * Background uninstall thread — Smart multi-stage package resolver
 * -------------------------------------------------------------------------
 *
 * Resolution order:
 *  1. Detect Flatpak  → flatpak uninstall --noninteractive <app-id>
 *  2. Detect Snap     → snap remove <name>
 *  3. dpkg -S <desktop_path>   (most reliable for .deb packages)
 *  4. dpkg -S <binary_path>    (resolve exec → full path via `which`)
 *  5. Hint from filename stem  (last resort)
 * ------------------------------------------------------------------------- */

typedef enum {
    APP_TYPE_DEB,
    APP_TYPE_FLATPAK,
    APP_TYPE_SNAP,
} AppType;

typedef struct {
    VenomUninstallDialog *dialog;
    char                 *desktop_path;   /* for dpkg -S */
    char                 *exec_binary;    /* first word of Exec= for binary lookup */
    char                 *package_hint;   /* filename-stem fallback */
    char                 *flatpak_id;     /* reverse-DNS id (org.app.Name) */
    char                 *snap_name;      /* snap instance name */
    AppType               app_type;
} UninstallTask;

typedef struct {
    VenomUninstallDialog *dialog;
    gboolean              success;
    char                 *detail_msg;
} UninstallResult;

/* Run a command synchronously, return stdout trimmed, or NULL on failure */
static char *
run_and_capture (char **argv)
{
    gchar  *sout        = NULL;
    gint    exit_status = 0;
    GError *err         = NULL;

    gboolean ok = g_spawn_sync (
        NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
        NULL, NULL, &sout, NULL, &exit_status, &err);

    g_clear_error (&err);
    if (!ok || exit_status != 0) {
        g_free (sout);
        return NULL;
    }
    if (sout) g_strchomp (sout);
    return sout;
}

/*
 * Query dpkg -S <path> and extract the package name.
 * dpkg output format: "package-name: /path/to/file"
 */
static char *
dpkg_search (const char *path)
{
    if (!path || !*path) return NULL;

    char *argv[] = { "dpkg", "-S", (char *) path, NULL };
    char *out    = run_and_capture (argv);
    if (!out) return NULL;

    /* Only use the first line (in case of multiple matches) */
    char *newline = strchr (out, '\n');
    if (newline) *newline = '\0';

    /* Extract package name before the colon */
    char *colon = strchr (out, ':');
    if (!colon) {
        g_free (out);
        return NULL;
    }
    *colon = '\0';
    /* Trim trailing whitespace from package name */
    g_strchomp (out);
    char *pkg = g_strdup (out);
    g_free (out);
    return pkg;
}

/*
 * Resolve the full path of a binary name using `which`.
 * Returns newly allocated string, or NULL if not found.
 */
static char *
which_binary (const char *name)
{
    if (!name || !*name || name[0] == '/') return g_strdup (name);
    char *argv[] = { "which", (char *) name, NULL };
    return run_and_capture (argv);
}

/*
 * Resolve the actual .deb package name using a cascade of strategies.
 * Returns newly allocated string (caller must g_free), or NULL.
 */
static char *
resolve_deb_package (const char *desktop_path,
                     const char *exec_binary,
                     const char *hint)
{
    char *pkg = NULL;

    /* Strategy 1: dpkg -S <desktop_path> */
    if (desktop_path) {
        pkg = dpkg_search (desktop_path);
        if (pkg) return pkg;
    }

    /* Strategy 2: dpkg -S <full_binary_path> */
    if (exec_binary) {
        char *bin_path = which_binary (exec_binary);
        if (bin_path) {
            pkg = dpkg_search (bin_path);
            g_free (bin_path);
            if (pkg) return pkg;
        }
    }

    /* Strategy 3: Use hint (filename stem or X-AppStream-Package) */
    if (hint && *hint) return g_strdup (hint);

    return NULL;
}

/* ── Main thread worker ─────────────────────────────────────────────────── */

static gboolean
on_uninstall_finished_idle (gpointer user_data)
{
    UninstallResult      *res  = user_data;
    VenomUninstallDialog *self = res->dialog;

    if (!GTK_IS_WIDGET (self)) {
        g_free (res->detail_msg);
        g_free (res);
        return G_SOURCE_REMOVE;
    }

    gtk_spinner_stop (GTK_SPINNER (self->spinner));

    if (res->success) {
        gtk_image_set_from_icon_name (GTK_IMAGE (self->result_icon),
                                      "emblem-ok-symbolic", GTK_ICON_SIZE_DIALOG);
        gtk_label_set_text (GTK_LABEL (self->result_label),
                            "\330\252\331\205 \330\247\331\204\330\255\330\260\331\201 \330\250\331\206\330\254\330\247\330\255");
        gtk_style_context_add_class (
            gtk_widget_get_style_context (self->result_label), "result-success");
    } else {
        gtk_image_set_from_icon_name (GTK_IMAGE (self->result_icon),
                                      "dialog-error", GTK_ICON_SIZE_DIALOG);
        gtk_label_set_text (GTK_LABEL (self->result_label),
                            "\331\201\330\264\331\204 \330\247\331\204\330\255\330\260\331\201");
        gtk_style_context_add_class (
            gtk_widget_get_style_context (self->result_label), "result-error");
    }

    if (res->detail_msg && *res->detail_msg)
        gtk_label_set_text (GTK_LABEL (self->result_detail), res->detail_msg);

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "result");
    gtk_widget_show_all (GTK_WIDGET (self));

    g_signal_emit (self, signals[SIGNAL_UNINSTALL_DONE], 0, res->success);

    g_free (res->detail_msg);
    g_free (res);
    return G_SOURCE_REMOVE;
}

static void
post_result (UninstallTask *task, gboolean success, char *detail_msg)
{
    UninstallResult *res = g_new0 (UninstallResult, 1);
    res->dialog     = task->dialog;
    res->success    = success;
    res->detail_msg = detail_msg;
    g_idle_add (on_uninstall_finished_idle, res);
}

static gpointer
uninstall_thread (gpointer user_data)
{
    UninstallTask *task = user_data;

    gboolean  success    = FALSE;
    char     *detail_msg = NULL;

    /* ── Flatpak ─────────────────────────────────────────────────────── */
    if (task->app_type == APP_TYPE_FLATPAK && task->flatpak_id) {
        char *cmd[] = {
            "flatpak", "uninstall", "--noninteractive", "-y",
            task->flatpak_id, NULL
        };
        gint    st  = 0;
        GError *err = NULL;
        gboolean ok = g_spawn_sync (NULL, cmd, NULL, G_SPAWN_SEARCH_PATH,
                                    NULL, NULL, NULL, NULL, &st, &err);
        if (ok && st == 0) {
            success    = TRUE;
            detail_msg = g_strdup_printf (
                "\330\252\331\205 \330\245\330\262\330\247\331\204\330\251 \330\250\330\261\331\206\330\247\331\205\330\254 Flatpak \"%s\" \330\250\331\206\330\254\330\247\330\255.",
                task->flatpak_id);
        } else {
            detail_msg = g_strdup_printf (
                "\331\201\330\264\331\204 flatpak uninstall (\330\243\331\203\331\210\330\257 %d).\n"
                "\330\252\330\243\331\203\331\221\330\257 \331\205\331\206 \330\252\330\253\330\250\331\212\330\252 flatpak \330\263\331\204\331\212\331\205.",
                st);
        }
        g_clear_error (&err);
        goto done;
    }

    /* ── Snap ────────────────────────────────────────────────────────── */
    if (task->app_type == APP_TYPE_SNAP && task->snap_name) {
        char *cmd[] = {
            "pkexec", "snap", "remove", task->snap_name, NULL
        };
        gint    st  = 0;
        GError *err = NULL;
        gboolean ok = g_spawn_sync (NULL, cmd, NULL, G_SPAWN_SEARCH_PATH,
                                    NULL, NULL, NULL, NULL, &st, &err);
        if (ok && st == 0) {
            success    = TRUE;
            detail_msg = g_strdup_printf (
                "\330\252\331\205 \330\245\330\262\330\247\331\204\330\251 \330\250\330\261\331\206\330\247\331\205\330\254 Snap \"%s\" \330\250\331\206\330\254\330\247\330\255.",
                task->snap_name);
        } else {
            detail_msg = g_strdup_printf (
                "\331\201\330\264\331\204 snap remove (\330\243\331\203\331\210\330\257 %d).",
                st);
        }
        g_clear_error (&err);
        goto done;
    }

    /* ── .deb (APT) ─────────────────────────────────────────────────── */
    {
        /* Resolve the real package name using dpkg database */
        char *pkg = resolve_deb_package (task->desktop_path,
                                         task->exec_binary,
                                         task->package_hint);
        if (!pkg) {
            detail_msg = g_strdup (
                "\331\204\331\205 \331\212\331\205\331\203\331\206 \330\252\330\255\330\257\331\212\330\257 \330\247\331\204\330\255\330\262\331\205\330\251 \330\250\330\247\330\263\330\252\330\256\330\257\330\247\331\205 dpkg.\n"
                "\330\252\330\255\331\202\331\221\331\202 \331\205\331\206 \330\243\331\206 \330\247\331\204\330\252\330\267\330\250\331\212\331\202 \331\205\330\253\330\250\331\221\330\252 \330\250\331\202\330\247\330\271\330\257\330\251 .deb.");
            goto done;
        }

        char *apt_cmd[] = {
            "pkexec", "apt-get", "remove", "-y", "--auto-remove", pkg, NULL
        };
        gint    st  = 0;
        GError *err = NULL;
        gchar  *sout = NULL, *serr = NULL;

        gboolean ok = g_spawn_sync (NULL, apt_cmd, NULL, G_SPAWN_SEARCH_PATH,
                                    NULL, NULL, &sout, &serr, &st, &err);
        if (ok && st == 0) {
            success    = TRUE;
            detail_msg = g_strdup_printf (
                "\330\252\331\205 \330\245\330\262\330\247\331\204\330\251 \330\247\331\204\330\255\330\262\331\205\330\251 \"%s\" \330\250\331\206\330\254\330\247\330\255.", pkg);
        } else if (!ok) {
            detail_msg = g_strdup_printf (
                "\330\252\330\271\330\260\331\221\330\261 \330\252\330\264\330\272\331\212\331\204 pkexec: %s",
                err ? err->message : "?");
        } else {
            detail_msg = g_strdup_printf (
                "\331\201\330\264\331\204 apt-get remove \"\%s\" (\330\243\331\203\331\210\330\257 %d).\n"
                "\330\252\330\255\331\202\331\221\331\202 \330\256\330\261\330\254 dpkg: \"%s\"",
                pkg, st,
                (serr && *serr) ? serr : "\330\261\330\275\330\266 \330\247\331\204\331\205\330\263\330\252\330\256\330\257\331\205");
        }

        g_free (sout);
        g_free (serr);
        g_clear_error (&err);
        g_free (pkg);
    }

done:
    post_result (task, success, detail_msg);

    g_free (task->desktop_path);
    g_free (task->exec_binary);
    g_free (task->package_hint);
    g_free (task->flatpak_id);
    g_free (task->snap_name);
    g_free (task);
    return NULL;
}

/* -------------------------------------------------------------------------
 * GObject boilerplate
 * ------------------------------------------------------------------------- */

static void
venom_uninstall_dialog_class_init (VenomUninstallDialogClass *klass)
{
    signals[SIGNAL_UNINSTALL_DONE] =
        g_signal_new ("uninstall-done",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 1, G_TYPE_BOOLEAN);

    signals[SIGNAL_DISMISS] =
        g_signal_new ("dismiss",
                      G_TYPE_FROM_CLASS (klass),
                      G_SIGNAL_RUN_LAST,
                      0, NULL, NULL, NULL,
                      G_TYPE_NONE, 0);
}

static void
venom_uninstall_dialog_init (VenomUninstallDialog *self)
{
    inject_dialog_css ();

    /* The outer widget IS the backdrop: full-screen, semi-transparent */
    gtk_orientable_set_orientation (GTK_ORIENTABLE (self), GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_hexpand (GTK_WIDGET (self), TRUE);
    gtk_widget_set_vexpand (GTK_WIDGET (self), TRUE);

    gtk_style_context_add_class (
        gtk_widget_get_style_context (GTK_WIDGET (self)), "uninstall-backdrop");

    /*
     * Center the card both horizontally and vertically.
     * We use a GtkBox with CENTER alignment — GtkOverlay's
     * pass-through is set to FALSE so clicks on the backdrop
     * don't fall through to the launcher icons.
     */
    self->card = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign (self->card, GTK_ALIGN_CENTER);
    gtk_widget_set_valign (self->card, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request (self->card, 400, -1);
    gtk_style_context_add_class (
        gtk_widget_get_style_context (self->card), "uninstall-card");

    gtk_box_pack_start (GTK_BOX (self), self->card, TRUE, TRUE, 0);

    /* Stack for the three pages */
    self->stack = gtk_stack_new ();
    gtk_stack_set_transition_type (GTK_STACK (self->stack),
                                   GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_stack_set_transition_duration (GTK_STACK (self->stack), 180);
    gtk_container_add (GTK_CONTAINER (self->card), self->stack);
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

GtkWidget *
venom_uninstall_dialog_new (AppEntry *entry)
{
    VenomUninstallDialog *self =
        g_object_new (VENOM_TYPE_UNINSTALL_DIALOG, NULL);

    self->entry = entry;

    /* Build pages now that entry is set */
    self->page_confirm  = build_confirm_page  (self);
    self->page_progress = build_progress_page (self);
    self->page_result   = build_result_page   (self);

    gtk_stack_add_named (GTK_STACK (self->stack), self->page_confirm,  "confirm");
    gtk_stack_add_named (GTK_STACK (self->stack), self->page_progress, "progress");
    gtk_stack_add_named (GTK_STACK (self->stack), self->page_result,   "result");

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "confirm");

    return GTK_WIDGET (self);
}

void
venom_uninstall_dialog_run_async (VenomUninstallDialog *self)
{
    g_return_if_fail (VENOM_IS_UNINSTALL_DIALOG (self));
    g_return_if_fail (self->entry != NULL);

    gtk_stack_set_visible_child_name (GTK_STACK (self->stack), "progress");
    gtk_spinner_start (GTK_SPINNER (self->spinner));
    gtk_widget_show_all (GTK_WIDGET (self));

    AppEntry      *e    = self->entry;
    UninstallTask *task = g_new0 (UninstallTask, 1);
    task->dialog        = self;
    task->desktop_path  = g_strdup (e->desktop_path);
    task->package_hint  = g_strdup (e->package_name); /* filename-stem fallback */

    /* ── Detect app type ──────────────────────────────────────────────── */
    const char *dpath = e->desktop_path ? e->desktop_path : "";

    if (strstr (dpath, "/flatpak/") || strstr (dpath, "flatpak")) {
        /* Flatpak: app-id is the .desktop filename without extension */
        task->app_type  = APP_TYPE_FLATPAK;
        char *base      = g_path_get_basename (dpath);
        if (g_str_has_suffix (base, ".desktop"))
            base[strlen (base) - 8] = '\0';
        task->flatpak_id = base; /* e.g. "org.gnome.Calculator" */

    } else if (strstr (dpath, "/snap/") || strstr (dpath, "snap")) {
        /* Snap: instance name is the filename stem */
        task->app_type = APP_TYPE_SNAP;
        char *base     = g_path_get_basename (dpath);
        if (g_str_has_suffix (base, ".desktop"))
            base[strlen (base) - 8] = '\0';
        task->snap_name = base;

    } else {
        /* Regular .deb — extract exec binary (first word of Exec=) */
        task->app_type = APP_TYPE_DEB;
        if (e->exec && *e->exec) {
            /* Copy up to the first space/tab */
            const char *end = e->exec;
            while (*end && *end != ' ' && *end != '\t') end++;
            task->exec_binary = g_strndup (e->exec, (gsize)(end - e->exec));
        }
    }

    GThread *thr = g_thread_new ("uninstall-worker", uninstall_thread, task);
    g_thread_unref (thr);
}
