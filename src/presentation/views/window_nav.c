#include "window_private.h"
#include <glib/gi18n.h>
#ifdef G_OS_UNIX
#include <gio/gdesktopappinfo.h>
#endif
#include <string.h>

static void update_path_css_class(AetherWindow *self) {
    GtkWidget *win = GTK_WIDGET(self);
    gtk_widget_remove_css_class(win, "path-root");
    gtk_widget_remove_css_class(win, "path-danger");
    
    if (!self->current_path) return;
    
    if (g_strcmp0(self->current_path, "/root") == 0) {
        gtk_widget_add_css_class(win, "path-root");
    } else if (g_str_has_prefix(self->current_path, "/etc") ||
               g_str_has_prefix(self->current_path, "/usr") ||
               g_str_has_prefix(self->current_path, "/bin") ||
               g_str_has_prefix(self->current_path, "/sbin") ||
               g_str_has_prefix(self->current_path, "/var") ||
               g_str_has_prefix(self->current_path, "/boot") ||
               g_str_has_prefix(self->current_path, "/sys") ||
               g_str_has_prefix(self->current_path, "/proc") ||
               g_str_has_prefix(self->current_path, "/dev") ||
               g_str_has_prefix(self->current_path, "/opt") ||
               g_strcmp0(self->current_path, "/") == 0) {
        gtk_widget_add_css_class(win, "path-danger");
    }
}

void load_directory(AetherWindow *self, const char *path) {
    if (self->current_path)
        g_ptr_array_add(self->back_stack, g_strdup(self->current_path));
    if (self->back_stack->len > HISTORY_MAX)
        g_ptr_array_remove_index(self->back_stack, 0);

    /* Clear forward stack on new navigation */
    g_ptr_array_set_size(self->fwd_stack, 0);

    char *new_path = g_strdup(path);
    g_free(self->current_path);
    self->current_path = new_path;
    update_path_css_class(self);
    update_pathbar(self);
    update_nav_buttons(self);
    aether_file_repository_list_directory_async(
        self->repo, self->current_path, NULL, on_directory_loaded, self);
}

void update_nav_buttons(AetherWindow *self) {
    gtk_widget_set_sensitive(self->btn_back, self->back_stack->len > 0);
    gtk_widget_set_sensitive(self->btn_fwd,  self->fwd_stack->len  > 0);
}

void update_statusbar(AetherWindow *self) {
    if (self->filter_model) {
        self->item_count = g_list_model_get_n_items(G_LIST_MODEL(self->filter_model));
    }
    char *text = g_strdup_printf("%u items", self->item_count);
    gtk_label_set_text(GTK_LABEL(self->status_label), text);
    g_free(text);

    GFile    *f     = g_file_parse_name(self->current_path ? self->current_path : "/");
    GFileInfo *info = g_file_query_filesystem_info(f,
                          G_FILE_ATTRIBUTE_FILESYSTEM_FREE,
                          NULL, NULL);
    g_object_unref(f);
    if (info) {
        guint64 free_bytes = g_file_info_get_attribute_uint64(info,
                                 G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
        g_object_unref(info);
        double free_gb = (double)free_bytes / (1024.0 * 1024.0 * 1024.0);
        char *space_text = g_strdup_printf("%.1f GB available", free_gb);
        gtk_label_set_text(GTK_LABEL(self->space_label), space_text);
        g_free(space_text);
    }
}

void update_pathbar(AetherWindow *self) {
    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(self->path_bar)) != NULL)
        gtk_box_remove(GTK_BOX(self->path_bar), child);

    if (!self->current_path) return;

    GFile *f = g_file_parse_name(self->current_path);
    GPtrArray *parents = g_ptr_array_new_with_free_func(g_object_unref);
    
    GFile *parent = g_object_ref(f);
    while (parent != NULL) {
        g_ptr_array_add(parents, parent);
        parent = g_file_get_parent(parent);
    }
    
    for (int i = parents->len - 1; i >= 0; i--) {
        GFile *curr = G_FILE(g_ptr_array_index(parents, i));
        char *name = g_file_get_basename(curr);
        char *path = g_file_get_parse_name(curr);
        char *uri = g_file_get_uri(curr);
        
        const char *label = name;
        if (!label || strlen(label) == 0 || g_strcmp0(label, "/") == 0) {
            if (g_str_has_prefix(uri, "trash:")) label = "Trash";
            else label = "Root";
        }
        
        GtkWidget *btn = gtk_button_new_with_label(label);
        gtk_widget_add_css_class(btn, "flat");
        g_object_set_data_full(G_OBJECT(btn), "path", g_strdup(path), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_path_button_clicked), self);
        gtk_box_append(GTK_BOX(self->path_bar), btn);
        
        if (i > 0) {
            GtkWidget *sep = gtk_label_new("›");
            gtk_widget_add_css_class(sep, "path-sep");
            gtk_widget_set_margin_start(sep, 2);
            gtk_widget_set_margin_end(sep, 2);
            gtk_box_append(GTK_BOX(self->path_bar), sep);
        }
        
        g_free(name);
        g_free(path);
        g_free(uri);
    }
    
    g_ptr_array_free(parents, TRUE);
    g_object_unref(f);
}

typedef struct {
    AetherWindow *window;
    GtkMultiSelection *grid_sel;
    GtkMultiSelection *list_sel;
    GtkFilterListModel *filter_model;
    GtkCustomFilter *name_filter;
    GtkCustomSorter *sorter;
} ModelUpdateData;

/* ── بيانات حوار تأكيد الصلاحيات ── */
typedef struct {
    AetherWindow *window;
    char         *path;
} ElevationAskData;

/* إعادة رسم العرض فوراً عند تغيير التحديد (ضروري لـ Rubber Band) */
static void on_selection_changed(GtkSelectionModel *model, guint pos, guint n_items, gpointer view) {
    (void)model; (void)pos; (void)n_items;
    gtk_widget_queue_draw(GTK_WIDGET(view));
}

static gboolean set_model_idle(gpointer user_data) {
    ModelUpdateData *d = user_data;

    /* أولاً: اجعل الـ views يشير لـ NULL لتجنب الوصول لموارد محررة */
    gtk_grid_view_set_model(GTK_GRID_VIEW(d->window->grid_view), NULL);
    gtk_column_view_set_model(GTK_COLUMN_VIEW(d->window->list_view), NULL);

    /* فصل الإشارات القديمة وتحرير النماذج السابقة */
    if (d->window->grid_sel) {
        g_signal_handlers_disconnect_by_func(d->window->grid_sel,
                                             on_selection_changed,
                                             d->window->grid_view);
        g_clear_object(&d->window->grid_sel);
    }
    if (d->window->list_sel) {
        g_signal_handlers_disconnect_by_func(d->window->list_sel,
                                             on_selection_changed,
                                             d->window->list_view);
        g_clear_object(&d->window->list_sel);
    }
    if (d->window->filter_model) {
        g_clear_object(&d->window->filter_model);
    }

    gtk_grid_view_set_model(GTK_GRID_VIEW(d->window->grid_view),
                            GTK_SELECTION_MODEL(d->grid_sel));
    gtk_column_view_set_model(GTK_COLUMN_VIEW(d->window->list_view),
                              GTK_SELECTION_MODEL(d->list_sel));

    /* احفظ المراجع وربط الإشارات للرسم الفوري أثناء Rubber Band */
    d->window->grid_sel = g_object_ref(d->grid_sel);
    d->window->list_sel = g_object_ref(d->list_sel);

    g_signal_connect(d->window->grid_sel, "selection-changed",
                     G_CALLBACK(on_selection_changed), d->window->grid_view);
    g_signal_connect(d->window->list_sel, "selection-changed",
                     G_CALLBACK(on_selection_changed), d->window->list_view);

    d->window->filter_model = d->filter_model;
    d->window->name_filter  = d->name_filter;
    d->window->sorter       = d->sorter;

    update_statusbar(d->window);

    g_object_unref(d->grid_sel);
    g_object_unref(d->list_sel);
    g_free(d);
    return G_SOURCE_REMOVE;
}


/* ── صلاحيات الجلسة: TRUE بعد أول موافقة من المستخدم ── */
static gboolean s_session_elevated = FALSE;

/* ── دالة رد الفعل على حوار تأكيد الصلاحيات ── */
static void on_elevation_ask_response(GtkDialog *dialog, int response_id, gpointer user_data) {
    ElevationAskData *data = user_data;
    gtk_window_destroy(GTK_WINDOW(dialog));

    if (response_id == GTK_RESPONSE_ACCEPT) {
        /* فعّل صلاحيات الجلسة: شغّل الـ daemon عبر pkexec مرة واحدة */
        s_session_elevated = TRUE;
        if (!aether_privileged_daemon_is_running()) {
            /* daemon_start يطلب كلمة المرور عبر pkexec — مرة واحدة للجلسة */
            if (!aether_privileged_daemon_start()) {
                /* فشل تشغيل الـ daemon */
                s_session_elevated = FALSE;
                show_elevation_error(data->window, data->path,
                                     "Failed to start privileged daemon.");
                navigate_back(data->window);
                g_free(data->path);
                g_free(data);
                return;
            }
        }
        load_directory_elevated(data->window, data->path);
    } else {
        /* المستخدم رفض: ارجع للمسار السابق */
        navigate_back(data->window);
    }

    g_free(data->path);
    g_free(data);
}


/* ── عرض حوار تأكيد طلب الصلاحيات أو التنفيذ المباشر ── */
static void ask_elevation(AetherWindow *self, const char *path) {
    /* إذا وافق المستخدم سابقاً في هذه الجلسة، نفّذ مباشرةً */
    if (s_session_elevated) {
        load_directory_elevated(self, path);
        return;
    }

    char *basename = g_path_get_basename(path);
    char *title    = g_strdup_printf("Administrator access required");
    char *msg      = g_strdup_printf(
        "The folder <b>%s</b> requires administrator privileges to open.\n\n"
        "You will be asked to enter your password.",
        basename);

    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(self),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_QUESTION,
        GTK_BUTTONS_NONE,
        "%s", title);

    gtk_message_dialog_format_secondary_markup(
        GTK_MESSAGE_DIALOG(dialog), "%s", msg);

    gtk_dialog_add_button(GTK_DIALOG(dialog), "Cancel",   GTK_RESPONSE_CANCEL);
    GtkWidget *open_btn = gtk_dialog_add_button(
        GTK_DIALOG(dialog), "Open as Administrator", GTK_RESPONSE_ACCEPT);
    gtk_widget_add_css_class(open_btn, "suggested-action");
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);

    ElevationAskData *data = g_new0(ElevationAskData, 1);
    data->window = self;
    data->path   = g_strdup(path);

    g_signal_connect(dialog, "response",
                     G_CALLBACK(on_elevation_ask_response), data);
    gtk_window_present(GTK_WINDOW(dialog));

    g_free(basename);
    g_free(title);
    g_free(msg);
}

void on_directory_loaded(GObject *source, GAsyncResult *res, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GError *err = NULL;
    GListModel *raw = aether_file_repository_list_directory_finish(
                          AETHER_FILE_REPOSITORY(source), res, &err);
    if (err) {
        /* ── اعتراض خطأ الصلاحيات: اسأل المستخدم فقط عند الحاجة الحقيقية ── */
        if (err->code == G_IO_ERROR_PERMISSION_DENIED &&
            self->current_path &&
            aether_privileged_is_available()) {
            /*
             * لا نطلب رفع الصلاحيات إذا كان المسار ضمن مجلد
             * المستخدم الحالي — المستخدم العادي يملك صلاحيات
             * كافية هناك، والخطأ يكون بسبب سبب آخر.
             */
            const char *home = g_get_home_dir();
            gboolean is_user_path = (home != NULL) &&
                                    g_str_has_prefix(self->current_path, home);
            if (!is_user_path) {
                g_error_free(err);
                ask_elevation(self, self->current_path);
                return;
            }
        }
        g_printerr("Directory load error: %s\n", err->message);
        g_error_free(err);
        return;
    }

    GListStore *visible = g_list_store_new(AETHER_TYPE_FILE_ENTITY);
    guint n = g_list_model_get_n_items(raw);
    for (guint i = 0; i < n; i++) {
        AetherFileEntity *e = AETHER_FILE_ENTITY(g_list_model_get_item(raw, i));
        g_list_store_append(visible, e);
        g_object_unref(e);
    }
    g_printerr("Loaded %u items\n", n);
    g_object_unref(raw);

    /* Sort with self context so sort_mode/sort_asc are used */
    GtkCustomSorter    *sorter   = gtk_custom_sorter_new(compare_entities, self, NULL);
    GtkSortListModel   *sorted   = gtk_sort_list_model_new(G_LIST_MODEL(visible), GTK_SORTER(sorter));
    GtkCustomFilter    *filter   = gtk_custom_filter_new(name_filter_func, self, NULL);
    GtkFilterListModel *filtered = gtk_filter_list_model_new(G_LIST_MODEL(sorted), GTK_FILTER(filter));

    GtkMultiSelection *grid_sel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(filtered)));
    GtkMultiSelection *list_sel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(filtered)));

    ModelUpdateData *d = g_new0(ModelUpdateData, 1);
    d->window = self;
    d->grid_sel = grid_sel;
    d->list_sel = list_sel;
    d->filter_model = filtered;
    d->name_filter = filter;
    d->sorter = sorter;

    g_idle_add(set_model_idle, d);

    /* Start file monitor for current directory */
    setup_file_monitor(self, self->current_path);
}

/* ──────────────────────────────────────────────────────────────
 * دوال الصلاحيات المرتفعة
 * ───────────────────────────────────────────────────────────── */

/**
 * load_directory_elevated:
 * يشغّل pkexec + aetherfiles-helper list وينتظر النتيجة.
 * يُستدعى تلقائياً عندما يرصد خطأ PERMISSION_DENIED.
 */
void load_directory_elevated(AetherWindow *self, const char *path) {
    /* أظهر Spinner للتلميح بأن الطلب جارٍ */
    if (self->progress_spinner)
        gtk_spinner_start(GTK_SPINNER(self->progress_spinner));

    aether_privileged_list_async(path, NULL,
                                  (GAsyncReadyCallback)on_elevated_list_done,
                                  self);
}

/**
 * on_elevated_list_done:
 * ينتهي عند وصول النتيجة من المساعد ويملأ العرض.
 */
void on_elevated_list_done(GObject *source, GAsyncResult *res, gpointer ud) {
    (void)source;
    AetherWindow *self = AETHER_WINDOW(ud);

    if (self->progress_spinner)
        gtk_spinner_stop(GTK_SPINNER(self->progress_spinner));

    GError *err = NULL;
    GListModel *raw = aether_privileged_list_finish(res, &err);

    if (err) {
        /* المستخدم رفض أو فشلت المصادقة */
        const char *msg = (err->code == G_IO_ERROR_CANCELLED ||
                           err->code == G_IO_ERROR_PERMISSION_DENIED)
            ? "Authentication was cancelled or denied."
            : err->message;

        show_elevation_error(self, self->current_path, msg);
        g_error_free(err);

        /* اعدل elevated_mode وارجع للمسار السابق */
        self->elevated_mode = FALSE;
        navigate_back(self);
        return;
    }

    /* نجاح: اضبط وضع الصلاحيات وملأ العرض */
    self->elevated_mode = TRUE;

    GListStore *visible = g_list_store_new(AETHER_TYPE_FILE_ENTITY);
    guint n = g_list_model_get_n_items(raw);
    for (guint i = 0; i < n; i++) {
        AetherFileEntity *e = AETHER_FILE_ENTITY(g_list_model_get_item(raw, i));
        g_list_store_append(visible, e);
        g_object_unref(e);
    }
    g_object_unref(raw);

    GtkCustomSorter    *sorter   = gtk_custom_sorter_new(compare_entities, self, NULL);
    GtkSortListModel   *sorted   = gtk_sort_list_model_new(G_LIST_MODEL(visible), GTK_SORTER(sorter));
    GtkCustomFilter    *filter   = gtk_custom_filter_new(name_filter_func, self, NULL);
    GtkFilterListModel *filtered = gtk_filter_list_model_new(G_LIST_MODEL(sorted), GTK_FILTER(filter));

    GtkMultiSelection *grid_sel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(filtered)));
    GtkMultiSelection *list_sel = gtk_multi_selection_new(G_LIST_MODEL(g_object_ref(filtered)));

    ModelUpdateData *d = g_new0(ModelUpdateData, 1);
    d->window     = self;
    d->grid_sel   = grid_sel;
    d->list_sel   = list_sel;
    d->filter_model = filtered;
    d->name_filter  = filter;
    d->sorter       = sorter;

    g_idle_add(set_model_idle, d);

    /* لا نفعّل file monitor للمسارات المحمية (pkexec لا يدعم GFileMonitor) */
}

/**
 * show_elevation_error:
 * يعرض رسالة خطأ بسيطة إذا فشلت العملية المحمية.
 */
void show_elevation_error(AetherWindow *self, const char *path, const char *message) {
    (void)path;
    GtkWidget *dialog = gtk_message_dialog_new(
        GTK_WINDOW(self),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        "فشلت العملية بصلاحيات مرتفعة");
    gtk_message_dialog_format_secondary_text(
        GTK_MESSAGE_DIALOG(dialog), "%s", message);
    g_signal_connect(dialog, "response",
                     G_CALLBACK(gtk_window_destroy), NULL);
    gtk_window_present(GTK_WINDOW(dialog));
}

void setup_file_monitor(AetherWindow *self, const char *path) {
    if (self->dir_monitor) {
        g_file_monitor_cancel(self->dir_monitor);
        g_clear_object(&self->dir_monitor);
    }
    if (!path) return;
    GFile *dir = g_file_parse_name(path);
    GError *err = NULL;
    self->dir_monitor = g_file_monitor_directory(dir,
                            G_FILE_MONITOR_WATCH_MOVES, NULL, &err);
    g_object_unref(dir);
    if (err) { g_error_free(err); return; }
    g_signal_connect(self->dir_monitor, "changed",
                     G_CALLBACK(on_dir_changed), self);
}

void navigate_back(AetherWindow *self) {
    if (self->back_stack->len == 0) return;
    if (self->current_path)
        g_ptr_array_add(self->fwd_stack, g_strdup(self->current_path));

    char *prev = g_ptr_array_steal_index(self->back_stack, self->back_stack->len - 1);
    g_free(self->current_path);
    self->current_path = prev;
    update_pathbar(self);
    update_nav_buttons(self);
    aether_file_repository_list_directory_async(
        self->repo, self->current_path, NULL, on_directory_loaded, self);
}

void navigate_forward(AetherWindow *self) {
    if (self->fwd_stack->len == 0) return;
    if (self->current_path)
        g_ptr_array_add(self->back_stack, g_strdup(self->current_path));

    char *next = g_ptr_array_steal_index(self->fwd_stack, self->fwd_stack->len - 1);
    g_free(self->current_path);
    self->current_path = next;
    update_pathbar(self);
    update_nav_buttons(self);
    aether_file_repository_list_directory_async(
        self->repo, self->current_path, NULL, on_directory_loaded, self);
}

void on_dir_changed(GFileMonitor *mon, GFile *file, GFile *other,
                           GFileMonitorEvent event, gpointer user_data)
{
    (void)mon; (void)file; (void)other;
    AetherWindow *self = AETHER_WINDOW(user_data);
    /* Debounce: only react to meaningful events */
    switch (event) {
    case G_FILE_MONITOR_EVENT_CREATED:
    case G_FILE_MONITOR_EVENT_DELETED:
    case G_FILE_MONITOR_EVENT_RENAMED:
    case G_FILE_MONITOR_EVENT_MOVED_IN:
    case G_FILE_MONITOR_EVENT_MOVED_OUT:
        if (self->current_path)
            aether_file_repository_list_directory_async(
                self->repo, self->current_path, NULL, on_directory_loaded, self);
        break;
    default:
        break;
    }
}

void on_path_button_clicked(GtkButton *btn, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    const char *path = g_object_get_data(G_OBJECT(btn), "path");
    if (path) load_directory(self, path);
}

void on_item_activated(GtkWidget *view, guint position, gpointer user_data) {
    AetherWindow *self = AETHER_WINDOW(user_data);
    GtkSelectionModel *sel = NULL;
    if (GTK_IS_GRID_VIEW(view))
        sel = gtk_grid_view_get_model(GTK_GRID_VIEW(view));
    else if (GTK_IS_COLUMN_VIEW(view))
        sel = gtk_column_view_get_model(GTK_COLUMN_VIEW(view));
    if (!sel) return;

    gpointer item = g_list_model_get_item(G_LIST_MODEL(sel), position);
    if (!item) return;
    AetherFileEntity *e = AETHER_FILE_ENTITY(item);
    if (aether_file_entity_is_directory(e)) {
        load_directory(self, aether_file_entity_get_path(e));
    } else {
        const char *path = aether_file_entity_get_path(e);
        const char *uri = aether_file_entity_get_uri(e);
        gboolean executed = FALSE;

        if (path && g_file_test(path, G_FILE_TEST_IS_EXECUTABLE)) {
            GFile *file = g_file_new_for_path(path);
            GFileInfo *info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                                                G_FILE_QUERY_INFO_NONE, NULL, NULL);
            if (info) {
                const char *ctype = g_file_info_get_content_type(info);
                if (ctype) {
                    if (g_content_type_is_a(ctype, "application/x-executable") ||
                        g_content_type_is_a(ctype, "application/x-shellscript") ||
                        g_content_type_is_a(ctype, "application/x-sharedlib") ||
                        g_content_type_is_a(ctype, "application/vnd.appimage") ||
                        g_str_has_suffix(path, ".AppImage") ||
                        g_str_has_suffix(path, ".sh") ||
                        g_str_has_suffix(path, ".run")) {
                        
                        GError *err = NULL;
                        char *dir = g_path_get_dirname(path);
                        char *argv[] = { (char *)path, NULL };
                        if (g_spawn_async(dir, argv, NULL,
                                          G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &err)) {
                            executed = TRUE;
                        } else {
                            g_printerr("Failed to execute application: %s\n", err->message);
                            g_error_free(err);
                        }
                        g_free(dir);
                    }
                }
                g_object_unref(info);
            }
            g_object_unref(file);
        }

#ifdef G_OS_UNIX
        /* Special handling for .desktop files */
        if (!executed && path && g_str_has_suffix(path, ".desktop")) {
            GDesktopAppInfo *app_info = g_desktop_app_info_new_from_filename(path);
            if (app_info) {
                GError *err = NULL;
                if (g_app_info_launch(G_APP_INFO(app_info), NULL, NULL, &err)) {
                    executed = TRUE;
                } else {
                    g_printerr("Failed to launch desktop file: %s\n", err->message);
                    g_error_free(err);
                }
                g_object_unref(app_info);
            }
        }
#endif

        if (!executed && uri) {
            g_app_info_launch_default_for_uri_async(uri, NULL, NULL, NULL, NULL);
        }
    }
    g_object_unref(item);
}

