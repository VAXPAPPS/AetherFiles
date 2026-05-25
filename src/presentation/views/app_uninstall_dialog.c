#include "app_uninstall_dialog.h"

#include <glib/gspawn.h>
#include <glib/gstdio.h>
#include <string.h>

enum {
    SIGNAL_UNINSTALL_DONE,
    N_SIGNALS
};
static guint signals[N_SIGNALS];

struct _AetherAppUninstallDialog {
    GtkWindow   parent_instance;

    AetherAppEntity *entry;
    
    GtkWidget  *card;
    GtkWidget  *stack;
    GtkWidget  *page_confirm;
    GtkWidget  *page_progress;
    GtkWidget  *page_result;

    GtkWidget  *spinner;
    GtkWidget  *status_label;
    GtkWidget  *result_icon;
    GtkWidget  *result_label;
    GtkWidget  *result_detail;
};

G_DEFINE_TYPE(AetherAppUninstallDialog, aether_app_uninstall_dialog, GTK_TYPE_WINDOW)

static void inject_dialog_css(void) {
    static gboolean done = FALSE;
    if (done) return;
    done = TRUE;

    static const char *css =
        ".uninstall-app-name { font-size: 17px; font-weight: bold; color: rgba(255,255,255,0.92); }"
        ".uninstall-warn { font-size: 13px; color: rgba(255,255,255,0.55); }"
        ".uninstall-confirm-btn { background: rgba(210,45,45,0.88); color: white; border-radius: 9px; padding: 7px 20px; border: none; font-weight: 600; font-size: 13px; }"
        ".uninstall-confirm-btn:hover { background: rgba(190,28,28,0.98); }"
        ".uninstall-cancel-btn { background: rgba(255,255,255,0.08); color: rgba(255,255,255,0.72); border-radius: 9px; padding: 7px 18px; border: none; font-size: 13px; }"
        ".uninstall-cancel-btn:hover { background: rgba(255,255,255,0.15); }"
        ".uninstall-status { color: rgba(255,255,255,0.65); font-size: 13px; }"
        ".uninstall-result-title { font-size: 16px; font-weight: bold; color: rgba(255,255,255,0.88); }"
        ".uninstall-result-detail { font-size: 12px; color: rgba(255,255,255,0.50); }"
        ".result-success { color: #4cda8a; }"
        ".result-error   { color: #e05555; }";

    GtkCssProvider *prov = gtk_css_provider_new();
    gtk_css_provider_load_from_data(prov, css, -1);
    gtk_style_context_add_provider_for_display(
        gdk_display_get_default(),
        GTK_STYLE_PROVIDER(prov),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 5);
}

static void on_cancel_clicked(GtkButton *btn, gpointer data) {
    (void)btn;
    AetherAppUninstallDialog *self = AETHER_APP_UNINSTALL_DIALOG(data);
    gtk_window_destroy(GTK_WINDOW(self));
}

static GtkWidget *build_confirm_page(AetherAppUninstallDialog *self) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 18);
    gtk_widget_set_margin_start(vbox, 32);
    gtk_widget_set_margin_end(vbox, 32);
    gtk_widget_set_margin_top(vbox, 28);
    gtk_widget_set_margin_bottom(vbox, 24);

    const char *icon_name = aether_app_entity_get_icon_name(self->entry);
    GtkWidget *icon_img;
    if (icon_name && g_path_is_absolute(icon_name)) {
        icon_img = gtk_image_new_from_file(icon_name);
    } else {
        icon_img = gtk_image_new_from_icon_name(icon_name ? icon_name : "application-x-executable");
    }
    gtk_image_set_pixel_size(GTK_IMAGE(icon_img), 64);
    gtk_widget_set_halign(icon_img, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(vbox), icon_img);

    const char *name = aether_app_entity_get_name(self->entry);
    GtkWidget *name_lbl = gtk_label_new(name ? name : "");
    gtk_widget_add_css_class(name_lbl, "uninstall-app-name");
    gtk_label_set_xalign(GTK_LABEL(name_lbl), 0.5f);
    gtk_box_append(GTK_BOX(vbox), name_lbl);

    char *warn_text = g_strdup("سيتم حذف اختصار التطبيق ومحاولة إزالته.\nهذا الإجراء لا يمكن التراجع عنه.");
    GtkWidget *warn = gtk_label_new(warn_text);
    g_free(warn_text);
    gtk_widget_add_css_class(warn, "uninstall-warn");
    gtk_label_set_justify(GTK_LABEL(warn), GTK_JUSTIFY_CENTER);
    gtk_label_set_wrap(GTK_LABEL(warn), TRUE);
    gtk_box_append(GTK_BOX(vbox), warn);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 4);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(vbox), sep);

    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    gtk_box_append(GTK_BOX(vbox), btn_box);

    GtkWidget *cancel_btn = gtk_button_new_with_label("إلغاء");
    gtk_widget_add_css_class(cancel_btn, "uninstall-cancel-btn");
    g_signal_connect(cancel_btn, "clicked", G_CALLBACK(on_cancel_clicked), self);
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);

    GtkWidget *confirm_btn = gtk_button_new_with_label("حذف التطبيق");
    gtk_widget_add_css_class(confirm_btn, "uninstall-confirm-btn");
    g_signal_connect_swapped(confirm_btn, "clicked", G_CALLBACK(aether_app_uninstall_dialog_run_async), self);
    gtk_box_append(GTK_BOX(btn_box), confirm_btn);

    return vbox;
}

static GtkWidget *build_progress_page(AetherAppUninstallDialog *self) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 20);
    gtk_widget_set_margin_start(vbox, 40);
    gtk_widget_set_margin_end(vbox, 40);
    gtk_widget_set_margin_top(vbox, 36);
    gtk_widget_set_margin_bottom(vbox, 36);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

    self->spinner = gtk_spinner_new();
    gtk_widget_set_halign(self->spinner, GTK_ALIGN_CENTER);
    gtk_widget_set_size_request(self->spinner, 48, 48);
    gtk_box_append(GTK_BOX(vbox), self->spinner);

    self->status_label = gtk_label_new("جارٍ إزالة التطبيق…");
    gtk_widget_add_css_class(self->status_label, "uninstall-status");
    gtk_box_append(GTK_BOX(vbox), self->status_label);

    return vbox;
}

static GtkWidget *build_result_page(AetherAppUninstallDialog *self) {
    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(vbox, 32);
    gtk_widget_set_margin_end(vbox, 32);
    gtk_widget_set_margin_top(vbox, 28);
    gtk_widget_set_margin_bottom(vbox, 24);
    gtk_widget_set_valign(vbox, GTK_ALIGN_CENTER);

    self->result_icon = gtk_image_new_from_icon_name("dialog-information");
    gtk_image_set_pixel_size(GTK_IMAGE(self->result_icon), 52);
    gtk_widget_set_halign(self->result_icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(vbox), self->result_icon);

    self->result_label = gtk_label_new("");
    gtk_widget_add_css_class(self->result_label, "uninstall-result-title");
    gtk_label_set_xalign(GTK_LABEL(self->result_label), 0.5f);
    gtk_box_append(GTK_BOX(vbox), self->result_label);

    self->result_detail = gtk_label_new("");
    gtk_widget_add_css_class(self->result_detail, "uninstall-result-detail");
    gtk_label_set_wrap(GTK_LABEL(self->result_detail), TRUE);
    gtk_label_set_justify(GTK_LABEL(self->result_detail), GTK_JUSTIFY_CENTER);
    gtk_label_set_max_width_chars(GTK_LABEL(self->result_detail), 38);
    gtk_box_append(GTK_BOX(vbox), self->result_detail);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_margin_top(sep, 4);
    gtk_widget_set_margin_bottom(sep, 4);
    gtk_box_append(GTK_BOX(vbox), sep);

    GtkWidget *close_btn = gtk_button_new_with_label("إغلاق");
    gtk_widget_add_css_class(close_btn, "uninstall-cancel-btn");
    gtk_widget_set_halign(close_btn, GTK_ALIGN_END);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_cancel_clicked), self);
    gtk_box_append(GTK_BOX(vbox), close_btn);

    return vbox;
}

/* -------------------------------------------------------------------------
 * Background uninstall thread — Smart multi-stage package resolver
 * ------------------------------------------------------------------------- */
typedef enum { APP_TYPE_DEB, APP_TYPE_FLATPAK, APP_TYPE_SNAP } AppType;

typedef struct {
    AetherAppUninstallDialog *dialog;
    char *desktop_path;
    char *exec_binary;
    char *package_hint;
    char *flatpak_id;
    char *snap_name;
    AppType app_type;
} UninstallTask;

typedef struct {
    AetherAppUninstallDialog *dialog;
    gboolean success;
    char *detail_msg;
} UninstallResult;

static char *run_and_capture(char **argv) {
    gchar *sout = NULL;
    gint exit_status = 0;
    GError *err = NULL;

    gboolean ok = g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                               NULL, NULL, &sout, NULL, &exit_status, &err);
    g_clear_error(&err);
    if (!ok || exit_status != 0) {
        g_free(sout);
        return NULL;
    }
    if (sout) g_strchomp(sout);
    return sout;
}

static char *dpkg_search(const char *path) {
    if (!path || !*path) return NULL;
    char *argv[] = { "dpkg", "-S", (char *)path, NULL };
    char *out = run_and_capture(argv);
    if (!out) return NULL;

    char *newline = strchr(out, '\n');
    if (newline) *newline = '\0';
    char *colon = strchr(out, ':');
    if (!colon) { g_free(out); return NULL; }
    *colon = '\0';
    g_strchomp(out);
    char *pkg = g_strdup(out);
    g_free(out);
    return pkg;
}

static char *which_binary(const char *name) {
    if (!name || !*name || name[0] == '/') return g_strdup(name);
    char *argv[] = { "which", (char *)name, NULL };
    return run_and_capture(argv);
}

static char *resolve_deb_package(const char *desktop_path, const char *exec_binary, const char *hint) {
    char *pkg = NULL;
    if (desktop_path) {
        pkg = dpkg_search(desktop_path);
        if (pkg) return pkg;
    }
    if (exec_binary) {
        char *bin_path = which_binary(exec_binary);
        if (bin_path) {
            pkg = dpkg_search(bin_path);
            g_free(bin_path);
            if (pkg) return pkg;
        }
    }
    if (hint && *hint) return g_strdup(hint);
    return NULL;
}

static gboolean on_uninstall_finished_idle(gpointer user_data) {
    UninstallResult *res = user_data;
    AetherAppUninstallDialog *self = res->dialog;

    if (!GTK_IS_WIDGET(self)) {
        g_free(res->detail_msg);
        g_free(res);
        return G_SOURCE_REMOVE;
    }

    gtk_spinner_stop(GTK_SPINNER(self->spinner));

    if (res->success) {
        gtk_image_set_from_icon_name(GTK_IMAGE(self->result_icon), "emblem-ok-symbolic");
        gtk_label_set_text(GTK_LABEL(self->result_label), "تم الحذف بنجاح");
        gtk_widget_add_css_class(self->result_label, "result-success");
    } else {
        gtk_image_set_from_icon_name(GTK_IMAGE(self->result_icon), "dialog-error");
        gtk_label_set_text(GTK_LABEL(self->result_label), "فشل الحذف");
        gtk_widget_add_css_class(self->result_label, "result-error");
    }

    if (res->detail_msg && *res->detail_msg)
        gtk_label_set_text(GTK_LABEL(self->result_detail), res->detail_msg);

    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "result");

    g_signal_emit(self, signals[SIGNAL_UNINSTALL_DONE], 0, res->success);

    g_free(res->detail_msg);
    g_free(res);
    return G_SOURCE_REMOVE;
}

static void post_result(UninstallTask *task, gboolean success, char *detail_msg) {
    UninstallResult *res = g_new0(UninstallResult, 1);
    res->dialog = task->dialog;
    res->success = success;
    res->detail_msg = detail_msg;
    g_idle_add(on_uninstall_finished_idle, res);
}

static gpointer uninstall_thread(gpointer user_data) {
    UninstallTask *task = user_data;
    gboolean success = FALSE;
    char *detail_msg = NULL;

    if (task->app_type == APP_TYPE_FLATPAK && task->flatpak_id) {
        char *cmd[] = { "flatpak", "uninstall", "--noninteractive", "-y", task->flatpak_id, NULL };
        gint st = 0; GError *err = NULL;
        gboolean ok = g_spawn_sync(NULL, cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &st, &err);
        if (ok && st == 0) {
            success = TRUE;
            detail_msg = g_strdup_printf("تم إزالة برنامج Flatpak \"%s\" بنجاح.", task->flatpak_id);
        } else {
            detail_msg = g_strdup_printf("فشل flatpak uninstall (كود %d).", st);
        }
        g_clear_error(&err);
        goto done;
    }

    if (task->app_type == APP_TYPE_SNAP && task->snap_name) {
        char *cmd[] = { "pkexec", "snap", "remove", task->snap_name, NULL };
        gint st = 0; GError *err = NULL;
        gboolean ok = g_spawn_sync(NULL, cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &st, &err);
        if (ok && st == 0) {
            success = TRUE;
            detail_msg = g_strdup_printf("تم إزالة Snap \"%s\" بنجاح.", task->snap_name);
        } else {
            detail_msg = g_strdup_printf("فشل snap remove (كود %d).", st);
        }
        g_clear_error(&err);
        goto done;
    }

    {
        char *pkg = resolve_deb_package(task->desktop_path, task->exec_binary, task->package_hint);
        if (!pkg) {
            detail_msg = g_strdup("لم يتم العثور على الحزمة باستخدام dpkg.");
            goto done;
        }

        char *apt_cmd[] = { "pkexec", "apt-get", "remove", "-y", "--auto-remove", pkg, NULL };
        gint st = 0; GError *err = NULL; gchar *sout = NULL, *serr = NULL;
        gboolean ok = g_spawn_sync(NULL, apt_cmd, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, &sout, &serr, &st, &err);
        
        if (ok && st == 0) {
            success = TRUE;
            detail_msg = g_strdup_printf("تم إزالة الحزمة \"%s\" بنجاح.", pkg);
        } else if (!ok) {
            detail_msg = g_strdup_printf("تعذر تشغيل pkexec: %s", err ? err->message : "?");
        } else {
            detail_msg = g_strdup_printf("فشل apt-get remove \"%s\" (كود %d).", pkg, st);
        }

        g_free(sout); g_free(serr); g_clear_error(&err); g_free(pkg);
    }

done:
    post_result(task, success, detail_msg);
    g_free(task->desktop_path);
    g_free(task->exec_binary);
    g_free(task->package_hint);
    g_free(task->flatpak_id);
    g_free(task->snap_name);
    g_free(task);
    return NULL;
}

static void aether_app_uninstall_dialog_class_init(AetherAppUninstallDialogClass *klass) {
    signals[SIGNAL_UNINSTALL_DONE] = g_signal_new(
        "uninstall-done", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
}

static void aether_app_uninstall_dialog_init(AetherAppUninstallDialog *self) {
    inject_dialog_css();
    gtk_window_set_title(GTK_WINDOW(self), "إزالة التطبيق");
    gtk_window_set_default_size(GTK_WINDOW(self), 400, 300);
    gtk_window_set_resizable(GTK_WINDOW(self), FALSE);

    self->card = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_halign(self->card, GTK_ALIGN_FILL);
    gtk_widget_set_valign(self->card, GTK_ALIGN_FILL);
    gtk_widget_add_css_class(self->card, "uninstall-card");
    gtk_window_set_child(GTK_WINDOW(self), self->card);

    self->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(self->stack), GTK_STACK_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_stack_set_transition_duration(GTK_STACK(self->stack), 180);
    gtk_box_append(GTK_BOX(self->card), self->stack);
}

GtkWidget *aether_app_uninstall_dialog_new(GtkWindow *parent, AetherAppEntity *entry) {
    AetherAppUninstallDialog *self = g_object_new(AETHER_TYPE_APP_UNINSTALL_DIALOG, NULL);
    if (parent) gtk_window_set_transient_for(GTK_WINDOW(self), parent);
    gtk_window_set_modal(GTK_WINDOW(self), TRUE);
    self->entry = entry;

    self->page_confirm = build_confirm_page(self);
    self->page_progress = build_progress_page(self);
    self->page_result = build_result_page(self);

    gtk_stack_add_named(GTK_STACK(self->stack), self->page_confirm, "confirm");
    gtk_stack_add_named(GTK_STACK(self->stack), self->page_progress, "progress");
    gtk_stack_add_named(GTK_STACK(self->stack), self->page_result, "result");
    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "confirm");

    return GTK_WIDGET(self);
}

void aether_app_uninstall_dialog_run_async(AetherAppUninstallDialog *self) {
    gtk_stack_set_visible_child_name(GTK_STACK(self->stack), "progress");
    gtk_spinner_start(GTK_SPINNER(self->spinner));

    AetherAppEntity *e = self->entry;
    UninstallTask *task = g_new0(UninstallTask, 1);
    task->dialog = self;
    
    const char *dpath = aether_app_entity_get_desktop_path(e);
    task->desktop_path = g_strdup(dpath);
    
    if (dpath && (strstr(dpath, "/flatpak/") || strstr(dpath, "flatpak"))) {
        task->app_type = APP_TYPE_FLATPAK;
        char *base = g_path_get_basename(dpath);
        if (g_str_has_suffix(base, ".desktop")) base[strlen(base) - 8] = '\0';
        task->flatpak_id = base;
    } else if (dpath && (strstr(dpath, "/snap/") || strstr(dpath, "snap"))) {
        task->app_type = APP_TYPE_SNAP;
        char *base = g_path_get_basename(dpath);
        if (g_str_has_suffix(base, ".desktop")) base[strlen(base) - 8] = '\0';
        task->snap_name = base;
    } else {
        task->app_type = APP_TYPE_DEB;
        const char *exec = aether_app_entity_get_exec(e);
        if (exec && *exec) {
            const char *end = exec;
            while (*end && *end != ' ' && *end != '\t') end++;
            task->exec_binary = g_strndup(exec, (gsize)(end - exec));
        }
    }

    GThread *thr = g_thread_new("uninstall-worker", uninstall_thread, task);
    g_thread_unref(thr);
}
