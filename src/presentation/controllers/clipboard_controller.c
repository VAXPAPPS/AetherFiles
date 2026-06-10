#include "clipboard_controller.h"
#include <glib.h>
#include <gio/gio.h>
#include <string.h>

struct _AetherClipboardController {
    GStrv            paths;
    AetherClipboardOp op;
};

AetherClipboardController *aether_clipboard_controller_new(void) {
    AetherClipboardController *self = g_new0(AetherClipboardController, 1);
    self->op    = AETHER_CLIPBOARD_NONE;
    self->paths = NULL;
    return self;
}

void aether_clipboard_controller_free(AetherClipboardController *self) {
    if (!self) return;
    g_strfreev(self->paths);
    g_free(self);
}

void aether_clipboard_set(AetherClipboardController *self, GStrv paths, AetherClipboardOp op) {
    g_strfreev(self->paths);
    self->paths = g_strdupv(paths);
    self->op    = op;
}

gboolean aether_clipboard_has_content(AetherClipboardController *self) {
    return self->paths != NULL && self->paths[0] != NULL && self->op != AETHER_CLIPBOARD_NONE;
}

AetherClipboardOp aether_clipboard_get_op(AetherClipboardController *self) {
    return self->op;
}

GStrv aether_clipboard_get_paths(AetherClipboardController *self) {
    return self->paths;
}

/* ── Paste ── */

typedef struct {
    AetherClipboardController *ctrl;
    GTask                     *task;
    int                        pending;
    int                        total;
    gboolean                   has_error;
    GError                    *first_error;
} PasteData;

static void check_paste_completion(PasteData *pd) {
    pd->pending--;
    if (pd->pending == 0) {
        if (pd->has_error) {
            g_task_return_error(pd->task, pd->first_error);
        } else {
            /* clear clipboard after successful cut */
            if (pd->ctrl->op == AETHER_CLIPBOARD_CUT) {
                g_strfreev(pd->ctrl->paths);
                pd->ctrl->paths = NULL;
                pd->ctrl->op    = AETHER_CLIPBOARD_NONE;
            }
            g_task_return_boolean(pd->task, TRUE);
        }
        g_object_unref(pd->task);
        g_free(pd);
    }
}

static void on_copy_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    PasteData *pd  = user_data;
    GError    *err = NULL;
    g_file_copy_finish(G_FILE(src), res, &err);
    if (err) {
        pd->has_error = TRUE;
        if (!pd->first_error) pd->first_error = err;
        else g_error_free(err);
    }
    check_paste_completion(pd);
}

static void on_move_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    PasteData *pd  = user_data;
    GError    *err = NULL;
    g_file_move_finish(G_FILE(src), res, &err);
    if (err) {
        pd->has_error = TRUE;
        if (!pd->first_error) pd->first_error = err;
        else g_error_free(err);
    }
    check_paste_completion(pd);
}

/* ── الـ paste العادي (بدون تحقق من التعارض) ── */
void aether_clipboard_paste(AetherClipboardController *self,
                             const char               *dest_dir,
                             GAsyncReadyCallback       cb,
                             gpointer                  user_data)
{
    aether_clipboard_paste_with_flags(self, dest_dir,
                                       G_FILE_COPY_NONE, cb, user_data);
}

void aether_clipboard_paste_with_flags(AetherClipboardController *self,
                                        const char               *dest_dir,
                                        GFileCopyFlags            flags,
                                        GAsyncReadyCallback       cb,
                                        gpointer                  user_data)
{
    if (!self->paths || !self->paths[0] || self->op == AETHER_CLIPBOARD_NONE) return;

    GTask *task = g_task_new(NULL, NULL, cb, user_data);
    PasteData *pd = g_new0(PasteData, 1);
    pd->ctrl = self;
    pd->task = g_object_ref(task);

    pd->total   = g_strv_length(self->paths);
    pd->pending = pd->total;
    pd->has_error  = FALSE;
    pd->first_error = NULL;

    for (int i = 0; i < pd->total; i++) {
        GFile *src      = g_file_new_for_path(self->paths[i]);
        char  *basename = g_path_get_basename(self->paths[i]);
        char  *dest_path = g_build_filename(dest_dir, basename, NULL);
        GFile *dest     = g_file_new_for_path(dest_path);

        if (self->op == AETHER_CLIPBOARD_COPY) {
            g_file_copy_async(src, dest,
                              flags,
                              G_PRIORITY_DEFAULT,
                              NULL, NULL, NULL,
                              on_copy_done, pd);
        } else { /* CUT */
            g_file_move_async(src, dest,
                              flags,
                              G_PRIORITY_DEFAULT,
                              NULL, NULL, NULL,
                              on_move_done, pd);
        }

        g_free(basename);
        g_free(dest_path);
        g_object_unref(src);
        g_object_unref(dest);
    }
    g_object_unref(task);
}

void aether_clipboard_paste_finish(AetherClipboardController *self,
                                    GAsyncResult              *res,
                                    GError                   **error)
{
    (void)self;
    g_task_propagate_boolean(G_TASK(res), error);
}

/* ── فحص التعارضات: يرجع قائمة أسماء الملفات المتعارضة ── */
GPtrArray *aether_clipboard_find_conflicts(AetherClipboardController *self,
                                            const char               *dest_dir)
{
    GPtrArray *conflicts = g_ptr_array_new_with_free_func(g_free);
    if (!self->paths) return conflicts;

    for (int i = 0; self->paths[i]; i++) {
        char  *basename  = g_path_get_basename(self->paths[i]);
        char  *dest_path = g_build_filename(dest_dir, basename, NULL);
        GFile *dest      = g_file_new_for_path(dest_path);

        if (g_file_query_exists(dest, NULL)) {
            /* تعارض: ملف بنفس الاسم موجود في الوجهة */
            g_ptr_array_add(conflicts, g_strdup(basename));
        }

        g_object_unref(dest);
        g_free(dest_path);
        g_free(basename);
    }
    return conflicts;
}

/* ── Paste Keep Both: ينسخ/ينقل مع تغيير الاسم عند التعارض ── */
void aether_clipboard_paste_keep_both(AetherClipboardController *self,
                                       const char               *dest_dir)
{
    if (!self->paths) return;

    for (int i = 0; self->paths[i]; i++) {
        GFile *src      = g_file_new_for_path(self->paths[i]);
        char  *basename = g_path_get_basename(self->paths[i]);
        GFile *dest_check = g_file_new_for_path(
                                g_build_filename(dest_dir, basename, NULL));
        gboolean conflict = g_file_query_exists(dest_check, NULL);
        g_object_unref(dest_check);

        char *dest_path = NULL;

        if (conflict) {
            /* انتزع الاسم والامتداد */
            char *dot       = strrchr(basename, '.');
            char *name_part = dot ? g_strndup(basename, dot - basename)
                                  : g_strdup(basename);
            const char *ext = dot ? dot : "";

            /* ابحث عن اسم متاح */
            int counter = 0;
            while (TRUE) {
                char *candidate;
                if (counter == 0)
                    candidate = g_strdup_printf("%s (copy)%s", name_part, ext);
                else
                    candidate = g_strdup_printf("%s (copy %d)%s", name_part,
                                                counter + 1, ext);
                dest_path = g_build_filename(dest_dir, candidate, NULL);
                g_free(candidate);
                GFile *test = g_file_new_for_path(dest_path);
                gboolean exists = g_file_query_exists(test, NULL);
                g_object_unref(test);
                if (!exists) break;
                g_free(dest_path);
                dest_path = NULL;
                counter++;
            }
            g_free(name_part);
        } else {
            dest_path = g_build_filename(dest_dir, basename, NULL);
        }

        GFile *dest = g_file_new_for_path(dest_path);
        GError *err = NULL;
        if (self->op == AETHER_CLIPBOARD_COPY)
            g_file_copy(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
        else
            g_file_move(src, dest, G_FILE_COPY_NONE, NULL, NULL, NULL, &err);
        if (err) {
            g_printerr("Keep-both error for %s: %s\n", basename, err->message);
            g_error_free(err);
        }

        g_object_unref(src);
        g_object_unref(dest);
        g_free(dest_path);
        g_free(basename);
    }

    /* مسح الـ clipboard عند القص */
    if (self->op == AETHER_CLIPBOARD_CUT) {
        g_strfreev(self->paths);
        self->paths = NULL;
        self->op    = AETHER_CLIPBOARD_NONE;
    }
}
