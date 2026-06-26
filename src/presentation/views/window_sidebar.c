#include "window_private.h"
#include <glib/gi18n.h>
#include "../../domain/drive_entity.h"
#include <glib/gstdio.h>

#define BOOKMARKS_FILE ".config/aetherfiles-bookmarks"

GtkWidget *make_sidebar_row(const char *name, const char *icon_name);

void load_bookmarks(AetherWindow *self) {
    /* Remove old bookmark rows first */
    GtkWidget *child = gtk_widget_get_first_child(self->sidebar_list);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        if (g_object_get_data(G_OBJECT(child), "is-bookmark")) {
            gtk_list_box_remove(GTK_LIST_BOX(self->sidebar_list), child);
        }
        child = next;
    }

    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *contents = NULL;
    if (!g_file_get_contents(bm_path, &contents, NULL, NULL)) {
        g_free(bm_path);
        return;
    }
    g_free(bm_path);

    char **lines = g_strsplit(contents, "\n", -1);
    g_free(contents);

    for (int i = 0; lines[i] != NULL; i++) {
        const char *line = lines[i];
        if (!line || line[0] == '\0') continue;
        /* Each line: file:///path [label] */
        char **parts = g_strsplit(line, " ", 2);
        const char *uri = parts[0];
        GFile *f = g_file_new_for_uri(uri);
        char  *path = g_file_get_path(f);
        g_object_unref(f);

        if (!path) { g_strfreev(parts); continue; }

        const char *label = (parts[1] && parts[1][0]) ?
                             parts[1] : g_path_get_basename(path);

        GtkWidget *bm_row = make_sidebar_row(label, "bookmark-new-symbolic");
        g_object_set_data(G_OBJECT(bm_row), "is-bookmark", GINT_TO_POINTER(1));
        g_object_set_data_full(G_OBJECT(bm_row), "path", g_strdup(path), g_free);
        gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), bm_row);

        g_free(path);
        g_strfreev(parts);
    }
    g_strfreev(lines);
}

void save_bookmark(const char *path) {
    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *existing = NULL;
    g_file_get_contents(bm_path, &existing, NULL, NULL);

    char *uri     = g_filename_to_uri(path, NULL, NULL);
    char *new_line = g_strdup_printf("%s\n", uri);
    char *new_contents;
    if (existing)
        new_contents = g_strconcat(existing, new_line, NULL);
    else
        new_contents = g_strdup(new_line);

    g_file_set_contents(bm_path, new_contents, -1, NULL);
    g_free(existing); g_free(uri); g_free(new_line);
    g_free(new_contents); g_free(bm_path);
}

void remove_bookmark(const char *path) {
    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *existing = NULL;
    if (!g_file_get_contents(bm_path, &existing, NULL, NULL)) {
        g_free(bm_path);
        return;
    }

    char *uri = g_filename_to_uri(path, NULL, NULL);
    char **lines = g_strsplit(existing, "\n", -1);
    GString *new_contents = g_string_new("");

    for (int i = 0; lines[i]; i++) {
        if (lines[i][0] == '\0') continue;
        char **parts = g_strsplit(lines[i], " ", 2);
        if (g_strcmp0(parts[0], uri) != 0) {
            g_string_append_printf(new_contents, "%s\n", lines[i]);
        }
        g_strfreev(parts);
    }

    g_file_set_contents(bm_path, new_contents->str, new_contents->len, NULL);

    g_string_free(new_contents, TRUE);
    g_strfreev(lines);
    g_free(uri);
    g_free(existing);
    g_free(bm_path);
}

gboolean is_bookmarked(const char *path) {
    char *bm_path = g_build_filename(g_get_home_dir(), BOOKMARKS_FILE, NULL);
    gchar *existing = NULL;
    if (!g_file_get_contents(bm_path, &existing, NULL, NULL)) {
        g_free(bm_path);
        return FALSE;
    }

    char *uri = g_filename_to_uri(path, NULL, NULL);
    char **lines = g_strsplit(existing, "\n", -1);
    gboolean found = FALSE;

    for (int i = 0; lines[i]; i++) {
        if (lines[i][0] == '\0') continue;
        char **parts = g_strsplit(lines[i], " ", 2);
        if (g_strcmp0(parts[0], uri) == 0) {
            found = TRUE;
        }
        g_strfreev(parts);
        if (found) break;
    }

    g_strfreev(lines);
    g_free(uri);
    g_free(existing);
    g_free(bm_path);
    return found;
}

static gboolean on_sidebar_row_drop_accept(GtkDropTarget *target, GdkDrop *drop, gpointer user_data) {
    (void)target; (void)drop;
    GtkWidget *row = GTK_WIDGET(user_data);
    const char *dest_path = g_object_get_data(G_OBJECT(row), "path");
    return dest_path != NULL;
}

typedef struct {
    GPtrArray    *deb_paths;
    AetherWindow *win;
    GtkWidget    *dialog;       /* نافذة التقدم */
    GtkWidget    *progress_bar;
    GtkWidget    *status_lbl;
    GtkWidget    *spinner;
    guint         pulse_id;     /* معرّف مؤقت النبض */
} InstallData;

/* ── تحديث العرض من الـ main thread بعد اكتمال التثبيت ── */
typedef struct {
    AetherWindow *win;
    GtkWidget    *dialog;
    gboolean      success;
} InstallDoneData;

static gboolean on_install_done_idle(gpointer user_data) {
    InstallDoneData *d = user_data;

    /* أغلق نافذة التقدم */
    if (d->dialog && GTK_IS_WIDGET(d->dialog))
        gtk_window_destroy(GTK_WINDOW(d->dialog));

    if (d->success && d->win && AETHER_IS_WINDOW(d->win)) {
        /* حدّث مستودع التطبيقات ثم انتقل لصفحة Apps */
        aether_app_repository_load_apps(d->win->app_repo);
        show_apps_view(d->win);
    }

    g_free(d);
    return G_SOURCE_REMOVE;
}

static gboolean pulse_progress(gpointer user_data) {
    InstallData *data = user_data;
    if (data->progress_bar && GTK_IS_WIDGET(data->progress_bar))
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(data->progress_bar));
    return G_SOURCE_CONTINUE;
}

static void on_install_process_exited(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    InstallData *data = user_data;
    GSubprocess *proc = G_SUBPROCESS(source_object);
    GError *err = NULL;
    g_subprocess_wait_finish(proc, res, &err);

    gboolean success = FALSE;
    if (err) {
        g_printerr("Install wait failed: %s\n", err->message);
        g_error_free(err);
    } else {
        int exit_code = g_subprocess_get_if_exited(proc)
                        ? g_subprocess_get_exit_status(proc) : -1;
        success = (exit_code == 0);
        if (!success) {
            /* اقرأ stderr لمعرفة سبب الفشل */
            GInputStream *stderr_stream = g_subprocess_get_stderr_pipe(proc);
            if (stderr_stream) {
                char buf[2048] = {0};
                gsize nread = 0;
                g_input_stream_read_all(stderr_stream, buf, sizeof(buf) - 1,
                                        &nread, NULL, NULL);
                if (nread > 0)
                    g_printerr("Install stderr: %s\n", buf);
            }
            g_printerr("Installation exited with code %d.\n", exit_code);
        }
    }

    /* إيقاف مؤقت النبض */
    if (data->pulse_id) {
        g_source_remove(data->pulse_id);
        data->pulse_id = 0;
    }

    /* أرسل النتيجة للـ main thread */
    InstallDoneData *done = g_new0(InstallDoneData, 1);
    done->win     = data->win;
    done->dialog  = data->dialog;
    done->success = success;
    g_idle_add(on_install_done_idle, done);

    g_ptr_array_free(data->deb_paths, TRUE);
    g_free(data);
    g_object_unref(proc);
}

/* ── التبديل من شاشة كلمة المرور إلى شاشة التقدم ── */
static void switch_to_progress_view(GtkWidget *dialog, InstallData *data) {
    GtkWidget *old_child = gtk_window_get_child(GTK_WINDOW(dialog));
    if (old_child) gtk_window_set_child(GTK_WINDOW(dialog), NULL);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);

    /* أيقونة تثبيت */
    GtkWidget *icon = gtk_image_new_from_icon_name("system-software-install-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *title = gtk_label_new("Installing packages…");
    gtk_widget_add_css_class(title, "title-4");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), title);

    /* Progress bar */
    GtkWidget *pbar = gtk_progress_bar_new();
    gtk_progress_bar_set_pulse_step(GTK_PROGRESS_BAR(pbar), 0.05);
    gtk_widget_set_hexpand(pbar, TRUE);
    gtk_box_append(GTK_BOX(box), pbar);
    data->progress_bar = pbar;

    /* Spinner */
    GtkWidget *spinner = gtk_spinner_new();
    gtk_spinner_start(GTK_SPINNER(spinner));
    gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), spinner);
    data->spinner = spinner;

    GtkWidget *hint = gtk_label_new("Please wait, do not close this window.");
    gtk_widget_add_css_class(hint, "dim-label");
    gtk_widget_set_halign(hint, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), hint);

    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);

    /* ابدأ نبض الـ progress bar كل 150ms */
    data->pulse_id = g_timeout_add(150, pulse_progress, data);
}

static void on_install_password_submit(GtkWidget *widget, gpointer user_data) {
    (void)user_data;
    GtkWidget *dialog = gtk_widget_get_ancestor(widget, GTK_TYPE_WINDOW);
    GtkWidget *entry  = g_object_get_data(G_OBJECT(dialog), "pwd-entry");
    InstallData *data  = g_object_get_data(G_OBJECT(dialog), "install-data");

    const char *pwd = gtk_editable_get_text(GTK_EDITABLE(entry));
    if (!pwd || strlen(pwd) == 0) return;

    /* بناء الأمر: sudo -S env DEBIAN_FRONTEND=noninteractive apt-get install -y */
    GPtrArray *cmd = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(cmd, g_strdup("sudo"));
    g_ptr_array_add(cmd, g_strdup("-S"));
    g_ptr_array_add(cmd, g_strdup("-E"));
    g_ptr_array_add(cmd, g_strdup("env"));
    g_ptr_array_add(cmd, g_strdup("DEBIAN_FRONTEND=noninteractive"));
    g_ptr_array_add(cmd, g_strdup("apt-get"));
    g_ptr_array_add(cmd, g_strdup("install"));
    g_ptr_array_add(cmd, g_strdup("-y"));
    g_ptr_array_add(cmd, g_strdup("--allow-downgrades"));
    g_ptr_array_add(cmd, g_strdup("--fix-broken"));
    for (guint i = 0; i < data->deb_paths->len; i++)
        g_ptr_array_add(cmd, g_strdup(g_ptr_array_index(data->deb_paths, i)));
    g_ptr_array_add(cmd, NULL);

    GError *err = NULL;
    GSubprocess *proc = g_subprocess_newv(
        (const char * const *)cmd->pdata,
        G_SUBPROCESS_FLAGS_STDIN_PIPE |
        G_SUBPROCESS_FLAGS_STDOUT_PIPE |
        G_SUBPROCESS_FLAGS_STDERR_PIPE,
        &err);

    if (proc) {
        /* أرسل كلمة المرور لـ sudo */
        char *pwd_nl = g_strdup_printf("%s\n", pwd);
        GOutputStream *stdin_stream = g_subprocess_get_stdin_pipe(proc);
        g_output_stream_write_all(stdin_stream, pwd_nl, strlen(pwd_nl), NULL, NULL, NULL);
        g_output_stream_close(stdin_stream, NULL, NULL);
        g_free(pwd_nl);

        /* احفظ مرجع النافذة في البيانات */
        data->dialog = dialog;

        /* بدّل الحوار لشاشة التقدم */
        switch_to_progress_view(dialog, data);

        g_subprocess_wait_async(proc, NULL, on_install_process_exited, data);
    } else {
        g_printerr("Failed to spawn sudo/dpkg: %s\n", err->message);
        g_error_free(err);
        g_ptr_array_free(data->deb_paths, TRUE);
        g_free(data);
        gtk_window_destroy(GTK_WINDOW(dialog));
    }
    g_ptr_array_free(cmd, TRUE);
}

static void show_install_auth_dialog(AetherWindow *win, GPtrArray *deb_paths) {
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Install Package");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 360, -1);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);

    /* أيقونة */
    GtkWidget *icon = gtk_image_new_from_icon_name("system-software-install-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), icon);

    GtkWidget *title = gtk_label_new("Administrator Password Required");
    gtk_widget_add_css_class(title, "title-4");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), title);

    /* عرض أسماء الحزم */
    GString *pkg_names = g_string_new("");
    for (guint i = 0; i < deb_paths->len; i++) {
        char *base = g_path_get_basename(g_ptr_array_index(deb_paths, i));
        if (i > 0) g_string_append(pkg_names, "\n");
        g_string_append(pkg_names, base);
        g_free(base);
    }
    GtkWidget *pkg_lbl = gtk_label_new(pkg_names->str);
    gtk_widget_add_css_class(pkg_lbl, "dim-label");
    gtk_label_set_wrap(GTK_LABEL(pkg_lbl), TRUE);
    gtk_widget_set_halign(pkg_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), pkg_lbl);
    g_string_free(pkg_names, TRUE);

    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_box_append(GTK_BOX(box), sep);

    GtkWidget *pwd_lbl = gtk_label_new("Enter your password to continue:");
    gtk_label_set_xalign(GTK_LABEL(pwd_lbl), 0.0f);
    gtk_box_append(GTK_BOX(box), pwd_lbl);

    GtkWidget *entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE);
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_box_append(GTK_BOX(box), entry);

    /* أزرار */
    GtkWidget *btn_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(btn_box, GTK_ALIGN_END);
    GtkWidget *cancel_btn = gtk_button_new_with_label("Cancel");
    GtkWidget *install_btn = gtk_button_new_with_label("Install");
    gtk_widget_add_css_class(install_btn, "suggested-action");
    gtk_box_append(GTK_BOX(btn_box), cancel_btn);
    gtk_box_append(GTK_BOX(btn_box), install_btn);
    gtk_box_append(GTK_BOX(box), btn_box);

    InstallData *data = g_new0(InstallData, 1);
    data->deb_paths = deb_paths;
    data->win       = win;
    data->dialog    = NULL;
    data->pulse_id  = 0;

    g_object_set_data(G_OBJECT(dialog), "pwd-entry",    entry);
    g_object_set_data(G_OBJECT(dialog), "install-data", data);

    g_signal_connect(install_btn, "clicked",  G_CALLBACK(on_install_password_submit), NULL);
    g_signal_connect(entry,       "activate", G_CALLBACK(on_install_password_submit), NULL);
    g_signal_connect_swapped(cancel_btn, "clicked", G_CALLBACK(gtk_window_destroy), dialog);

    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_window_present(GTK_WINDOW(dialog));
}

typedef struct {
    GPtrArray *appimage_paths;
    AetherWindow *win;
    GtkWidget *dialog;
    GtkWidget *progress_bar;
    GtkWidget *status_lbl;
    guint total;
    guint current;
} AppImageInstallData;

static void extract_and_integrate_appimage(const char *appimage_path, const char *app_name) {
    char *temp_dir = g_dir_make_tmp("aether_appimage_XXXXXX", NULL);
    
    char *apps_dir = g_build_filename(g_get_home_dir(), ".local", "share", "applications", NULL);
    g_mkdir_with_parents(apps_dir, 0755);
    char *desktop_filename = g_strdup_printf("aether-%s.desktop", app_name);
    char *final_desktop_path = g_build_filename(apps_dir, desktop_filename, NULL);
    g_free(desktop_filename);
    
    if (!temp_dir) {
        goto fallback;
    }

    /* Extract .desktop */
    char *cmd_desktop[] = { (char *)appimage_path, "--appimage-extract", "*.desktop", NULL };
    g_spawn_sync(temp_dir, cmd_desktop, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, NULL, NULL);

    /* Extract .DirIcon */
    char *cmd_icon[] = { (char *)appimage_path, "--appimage-extract", ".DirIcon", NULL };
    g_spawn_sync(temp_dir, cmd_icon, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, NULL, NULL);

    char *sq_root = g_build_filename(temp_dir, "squashfs-root", NULL);
    char *diricon_path = g_build_filename(sq_root, ".DirIcon", NULL);

    /* Resolve symlink if needed */
    char *real_icon_path = g_strdup(diricon_path);
    if (g_file_test(diricon_path, G_FILE_TEST_IS_SYMLINK)) {
        char *target = g_file_read_link(diricon_path, NULL);
        if (target) {
            char *cmd_target[] = { (char *)appimage_path, "--appimage-extract", target, NULL };
            g_spawn_sync(temp_dir, cmd_target, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, NULL, NULL);
            g_free(real_icon_path);
            real_icon_path = g_build_filename(sq_root, target, NULL);
            g_free(target);
        }
    }

    /* Find the extracted .desktop file */
    char *desktop_file = NULL;
    GDir *dir = g_dir_open(sq_root, 0, NULL);
    if (dir) {
        const char *name;
        while ((name = g_dir_read_name(dir)) != NULL) {
            if (g_str_has_suffix(name, ".desktop")) {
                desktop_file = g_build_filename(sq_root, name, NULL);
                break;
            }
        }
        g_dir_close(dir);
    }

    char *icons_dir = g_build_filename(g_get_home_dir(), ".local", "share", "icons", NULL);
    g_mkdir_with_parents(icons_dir, 0755);
    char *icon_dest_name = g_strdup_printf("aether-%s", app_name);
    char *icon_dest = NULL;
    
    /* If we have the icon, copy it */
    if (g_file_test(real_icon_path, G_FILE_TEST_EXISTS)) {
        const char *ext = ".png";
        if (g_str_has_suffix(real_icon_path, ".svg")) ext = ".svg";
        
        char *icon_filename = g_strdup_printf("%s%s", icon_dest_name, ext);
        icon_dest = g_build_filename(icons_dir, icon_filename, NULL);
        g_free(icon_filename);

        GFile *f_src = g_file_new_for_path(real_icon_path);
        GFile *f_dst = g_file_new_for_path(icon_dest);
        g_file_copy(f_src, f_dst, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
        g_object_unref(f_src);
        g_object_unref(f_dst);
    }

    GKeyFile *kf = g_key_file_new();
    if (desktop_file && g_key_file_load_from_file(kf, desktop_file, G_KEY_FILE_KEEP_COMMENTS, NULL)) {
        /* Update existing .desktop */
        g_key_file_set_string(kf, "Desktop Entry", "Exec", appimage_path);
        if (icon_dest) {
            g_key_file_set_string(kf, "Desktop Entry", "Icon", icon_dest_name);
        }
        g_key_file_save_to_file(kf, final_desktop_path, NULL);
    } else {
        goto fallback;
    }

    g_key_file_free(kf);
    if (icon_dest) g_free(icon_dest);
    g_free(icon_dest_name);
    g_free(icons_dir);
    if (desktop_file) g_free(desktop_file);
    g_free(real_icon_path);
    g_free(diricon_path);
    g_free(sq_root);
    g_free(final_desktop_path);
    g_free(apps_dir);
    
    /* Cleanup temp dir */
    char *rm_cmd[] = { "rm", "-rf", temp_dir, NULL };
    g_spawn_sync(NULL, rm_cmd, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, NULL, NULL);
    g_free(temp_dir);
    return;

fallback:
    /* Fallback generic .desktop */
    {
        GKeyFile *kf_fb = g_key_file_new();
        g_key_file_set_string(kf_fb, "Desktop Entry", "Name", app_name);
        g_key_file_set_string(kf_fb, "Desktop Entry", "Exec", appimage_path);
        g_key_file_set_string(kf_fb, "Desktop Entry", "Icon", "application-x-executable");
        g_key_file_set_string(kf_fb, "Desktop Entry", "Type", "Application");
        g_key_file_set_string(kf_fb, "Desktop Entry", "Categories", "Utility;");
        g_key_file_save_to_file(kf_fb, final_desktop_path, NULL);
        g_key_file_free(kf_fb);
    }
    g_free(final_desktop_path);
    g_free(apps_dir);
    if (temp_dir) {
        char *rm_cmd[] = { "rm", "-rf", temp_dir, NULL };
        g_spawn_sync(NULL, rm_cmd, NULL, G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, NULL, NULL);
        g_free(temp_dir);
    }
}

static void process_next_appimage(AppImageInstallData *data);

static void on_appimage_copied(GObject *source_object, GAsyncResult *res, gpointer user_data) {
    AppImageInstallData *data = user_data;
    GFile *src = G_FILE(source_object);
    GError *err = NULL;
    
    g_file_copy_finish(src, res, &err);
    if (err) {
        g_printerr("Failed to copy AppImage: %s\n", err->message);
        g_error_free(err);
    } else {
        const char *src_path = g_ptr_array_index(data->appimage_paths, data->current);
        char *basename = g_path_get_basename(src_path);
        char *dest_path = g_build_filename(g_get_home_dir(), "Applications", basename, NULL);
        
        /* Make executable */
        g_chmod(dest_path, 0755);
        
        /* Create desktop file */
        char *name_no_ext = g_strdup(basename);
        char *dot = strrchr(name_no_ext, '.');
        if (dot) *dot = '\0';
        
        extract_and_integrate_appimage(dest_path, name_no_ext);
        
        g_free(name_no_ext);
        g_free(dest_path);
        g_free(basename);
    }
    
    data->current++;
    process_next_appimage(data);
}

static void process_next_appimage(AppImageInstallData *data) {
    if (data->current >= data->total) {
        if (data->dialog) gtk_window_destroy(GTK_WINDOW(data->dialog));
        if (data->win && AETHER_IS_WINDOW(data->win)) {
            aether_app_repository_load_apps(data->win->app_repo);
            show_apps_view(data->win);
        }
        g_ptr_array_free(data->appimage_paths, TRUE);
        g_free(data);
        return;
    }
    
    if (data->progress_bar) {
        double frac = (double)data->current / data->total;
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(data->progress_bar), frac);
    }
    
    const char *src_path = g_ptr_array_index(data->appimage_paths, data->current);
    char *basename = g_path_get_basename(src_path);
    char *apps_dir = g_build_filename(g_get_home_dir(), "Applications", NULL);
    g_mkdir_with_parents(apps_dir, 0755);
    
    char *dest_path = g_build_filename(apps_dir, basename, NULL);
    
    GFile *src_file = g_file_new_for_path(src_path);
    GFile *dest_file = g_file_new_for_path(dest_path);
    
    /* Update label */
    if (data->status_lbl) {
        char *msg = g_strdup_printf("Copying %s...", basename);
        gtk_label_set_text(GTK_LABEL(data->status_lbl), msg);
        g_free(msg);
    }
    
    g_file_copy_async(src_file, dest_file, G_FILE_COPY_OVERWRITE, G_PRIORITY_DEFAULT, NULL, NULL, NULL, on_appimage_copied, data);
    
    g_object_unref(src_file);
    g_object_unref(dest_file);
    g_free(dest_path);
    g_free(apps_dir);
    g_free(basename);
}

static void install_appimages_async(AetherWindow *win, GPtrArray *appimage_paths) {
    AppImageInstallData *data = g_new0(AppImageInstallData, 1);
    data->appimage_paths = appimage_paths;
    data->win = win;
    data->total = appimage_paths->len;
    data->current = 0;
    
    GtkWidget *dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog), "Installing AppImages");
    gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(win));
    gtk_window_set_modal(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 360, -1);
    gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);
    
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);
    
    GtkWidget *icon = gtk_image_new_from_icon_name("system-software-install-symbolic");
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 48);
    gtk_widget_set_halign(icon, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), icon);
    
    GtkWidget *title = gtk_label_new("Installing AppImages...");
    gtk_widget_add_css_class(title, "title-4");
    gtk_widget_set_halign(title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), title);
    
    data->progress_bar = gtk_progress_bar_new();
    gtk_widget_set_hexpand(data->progress_bar, TRUE);
    gtk_box_append(GTK_BOX(box), data->progress_bar);
    
    data->status_lbl = gtk_label_new("Starting...");
    gtk_widget_add_css_class(data->status_lbl, "dim-label");
    gtk_widget_set_halign(data->status_lbl, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(box), data->status_lbl);
    
    gtk_window_set_child(GTK_WINDOW(dialog), box);
    gtk_window_present(GTK_WINDOW(dialog));
    
    data->dialog = dialog;
    
    process_next_appimage(data);
}

static gboolean on_sidebar_row_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data) {
    (void)target; (void)x; (void)y;
    GtkWidget *row = GTK_WIDGET(user_data);
    const char *dest_path = g_object_get_data(G_OBJECT(row), "path");
    
    if (!dest_path) return FALSE;
    
    GdkFileList *file_list = g_value_get_boxed(value);
    if (!file_list) return FALSE;
    
    GSList *files = gdk_file_list_get_files(file_list);
    gboolean success = TRUE;
    
    gboolean is_trash = g_strcmp0(dest_path, "trash:///") == 0;
    gboolean is_apps  = g_strcmp0(dest_path, "apps:///") == 0;
    
    if (is_apps) {
        GPtrArray *deb_paths = g_ptr_array_new_with_free_func(g_free);
        GPtrArray *appimage_paths = g_ptr_array_new_with_free_func(g_free);

        for (GSList *l = files; l != NULL; l = l->next) {
            GFile *src = G_FILE(l->data);
            char *path = g_file_get_path(src);
            if (!path) continue;
            
            char *lower = g_ascii_strdown(path, -1);
            if (g_str_has_suffix(lower, ".deb")) {
                g_ptr_array_add(deb_paths, path);
            } else if (g_str_has_suffix(lower, ".appimage")) {
                g_ptr_array_add(appimage_paths, path);
            } else {
                g_free(path);
            }
            g_free(lower);
        }
        
        GtkWidget *win_widget = gtk_widget_get_ancestor(row, AETHER_TYPE_WINDOW);
        
        if (deb_paths->len > 0) {
            if (win_widget) {
                show_install_auth_dialog(AETHER_WINDOW(win_widget), deb_paths);
            } else {
                g_ptr_array_free(deb_paths, TRUE);
            }
        } else {
            g_ptr_array_free(deb_paths, TRUE);
        }

        if (appimage_paths->len > 0) {
            if (win_widget) {
                install_appimages_async(AETHER_WINDOW(win_widget), appimage_paths);
            } else {
                g_ptr_array_free(appimage_paths, TRUE);
            }
        } else {
            g_ptr_array_free(appimage_paths, TRUE);
        }
        
        return success;
    }
    
    if (is_trash) {
        for (GSList *l = files; l != NULL; l = l->next) {
            GFile *src = G_FILE(l->data);
            GError *err = NULL;
            g_file_trash(src, NULL, &err);
            if (err) {
                g_printerr("Sidebar DnD error: Error trashing file: %s\n", err->message);
                g_error_free(err);
                success = FALSE;
            }
        }
        
        GtkWidget *win_widget = gtk_widget_get_ancestor(row, AETHER_TYPE_WINDOW);
        if (win_widget) {
            aether_window_reload(AETHER_WINDOW(win_widget));
        }
        return success;
    }

    /* Regular directory drop target: handle with conflict resolution */
    GtkWidget *win_widget = gtk_widget_get_ancestor(row, AETHER_TYPE_WINDOW);
    if (!win_widget) return FALSE;
    AetherWindow *win = AETHER_WINDOW(win_widget);

    GPtrArray *src_paths = g_ptr_array_new_with_free_func(g_free);
    for (GSList *l = files; l != NULL; l = l->next) {
        GFile *src = G_FILE(l->data);
        char *src_path = g_file_get_path(src);
        if (src_path) {
            g_ptr_array_add(src_paths, src_path);
        }
    }

    if (src_paths->len > 0) {
        aether_window_handle_dnd_move(win, dest_path, src_paths);
    } else {
        g_ptr_array_free(src_paths, TRUE);
    }
    
    return TRUE;
}

GtkWidget *make_sidebar_row(const char *name, const char *icon_name) {
    GtkWidget *row = gtk_list_box_row_new();
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_set_margin_start(box, 8);
    gtk_widget_set_margin_end(box, 8);
    gtk_widget_set_margin_top(box, 4);
    gtk_widget_set_margin_bottom(box, 4);

    GtkWidget *icon  = gtk_image_new_from_icon_name(icon_name);
    gtk_image_set_pixel_size(GTK_IMAGE(icon), 16);
    GtkWidget *label = gtk_label_new(name);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
    gtk_widget_set_hexpand(label, TRUE);

    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), label);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    
    GtkDropTarget *row_drop = gtk_drop_target_new(GDK_TYPE_FILE_LIST, GDK_ACTION_COPY | GDK_ACTION_MOVE);
    g_signal_connect(row_drop, "accept", G_CALLBACK(on_sidebar_row_drop_accept), row);
    g_signal_connect(row_drop, "drop", G_CALLBACK(on_sidebar_row_drop), row);
    gtk_widget_add_controller(row, GTK_EVENT_CONTROLLER(row_drop));
    
    return row;
}

void add_sidebar_separator(AetherWindow *self) {
    GtkWidget *sep = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_widget_set_margin_top(sep, 0);
    gtk_widget_set_margin_bottom(sep, 0);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), sep);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

void add_sidebar_header(AetherWindow *self, const char *title) {
    GtkWidget *lbl = gtk_label_new(title);
    gtk_widget_add_css_class(lbl, "sidebar-section-label");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0f);
    gtk_widget_set_sensitive(lbl, FALSE);

    GtkWidget *row = gtk_list_box_row_new();
    gtk_list_box_row_set_activatable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

void add_sidebar_place(AetherWindow *self, const char *name,
                               const char *icon, const char *path)
{
    GtkWidget *row = make_sidebar_row(name, icon);
    g_object_set_data_full(G_OBJECT(row), "path", g_strdup(path), g_free);
    gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
}

static void refresh_devices_sidebar(AetherDriveManager *mgr, AetherWindow *self) {
    (void)mgr;
    /* Remove old device rows */
    GtkWidget *child = gtk_widget_get_first_child(self->sidebar_list);
    while (child) {
        GtkWidget *next = gtk_widget_get_next_sibling(child);
        if (g_object_get_data(G_OBJECT(child), "is-device")) {
            gtk_list_box_remove(GTK_LIST_BOX(self->sidebar_list), child);
        }
        child = next;
    }

    GListStore *drives = aether_drive_manager_get_drives(self->drive_mgr);
    guint n = g_list_model_get_n_items(G_LIST_MODEL(drives));
    for (guint i = 0; i < n; i++) {
        AetherDriveEntity *entity = AETHER_DRIVE_ENTITY(g_list_model_get_item(G_LIST_MODEL(drives), i));
        
        GtkWidget *row = make_sidebar_row(aether_drive_entity_get_name(entity), aether_drive_entity_get_icon_name(entity));
        g_object_set_data(G_OBJECT(row), "is-device", GINT_TO_POINTER(1));
        
        if (aether_drive_entity_is_mounted(entity)) {
            g_object_set_data_full(G_OBJECT(row), "path", g_strdup(aether_drive_entity_get_path(entity)), g_free);
        } else {
            GVolume *vol = aether_drive_entity_get_volume(entity);
            if (vol) g_object_set_data_full(G_OBJECT(row), "volume", g_object_ref(vol), g_object_unref);
        }
        
        gtk_list_box_append(GTK_LIST_BOX(self->sidebar_list), row);
        g_object_unref(entity);
    }
}

void setup_sidebar(AetherWindow *self) {
    self->sidebar_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(self->sidebar_list), GTK_SELECTION_SINGLE);
    g_signal_connect(self->sidebar_list, "row-activated",
                     G_CALLBACK(on_sidebar_row_activated), self);
    gtk_widget_add_css_class(self->sidebar_list, "navigation-sidebar");

    /* Pinned */

    add_sidebar_place(self, "Home",      "user-home-symbolic",            g_get_home_dir());

    char *work_path = g_build_filename(g_get_home_dir(), "Work", NULL);
    GFile *work_dir_file = g_file_new_for_path(work_path);
    if (!g_file_query_exists(work_dir_file, NULL)) {
        g_file_make_directory_with_parents(work_dir_file, NULL, NULL);
    }
    g_object_unref(work_dir_file);
    add_sidebar_place(self, "Work",      "folder-development-symbolic",   work_path);
    g_free(work_path);

    add_sidebar_place(self, "Apps",      "application-x-executable-symbolic", "apps:///");


    /* Places */
    add_sidebar_place(self, "Desktop",   "user-desktop-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP));
    add_sidebar_place(self, "Documents", "folder-documents-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS));
    add_sidebar_place(self, "Downloads", "folder-download-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD));
    add_sidebar_place(self, "Music",     "folder-music-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_MUSIC));
    add_sidebar_place(self, "Pictures",  "folder-pictures-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_PICTURES));
    add_sidebar_place(self, "Videos",    "folder-videos-symbolic",
                      g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS));
    add_sidebar_place(self, "Trash",     "user-trash-symbolic",           "trash:///");


    /* Drives */

    g_signal_connect(self->drive_mgr, "drives-changed", G_CALLBACK(refresh_devices_sidebar), self);
    refresh_devices_sidebar(self->drive_mgr, self);
}

static void on_mount_ready(GObject *source, GAsyncResult *res, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GError *err = NULL;
    if (aether_drive_manager_mount_finish(self->drive_mgr, res, &err)) {
        /* Success, the drives-changed signal will fire and update the sidebar.
           We could try to navigate to it, but finding the path here is tricky without the entity.
           Let's just let it mount for now. */
    } else {
        g_printerr("Failed to mount: %s\n", err->message);
        g_error_free(err);
    }
}

void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    (void)box;
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(row), "path");
    GVolume *vol = g_object_get_data(G_OBJECT(row), "volume");
    
    if (path) {
        if (g_strcmp0(path, "apps:///") == 0) {
            show_apps_view(self);
        } else {
            /* If we navigate to a real path, show grid or list view again */
            if (self->view_stack) {
                if (gtk_widget_has_css_class(self->btn_grid, "active-view")) {
                    gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), "grid");
                } else {
                    gtk_stack_set_visible_child_name(GTK_STACK(self->view_stack), "list");
                }
                gtk_widget_set_visible(self->path_bar, TRUE);
                gtk_widget_set_visible(self->sort_btn, TRUE);
                gtk_widget_set_visible(self->btn_grid, TRUE);
                gtk_widget_set_visible(self->btn_list, TRUE);
            }
            load_directory(self, path);
        }
    } else if (vol) {
        aether_drive_manager_mount_async(self->drive_mgr, vol, NULL, on_mount_ready, self);
    }
}

