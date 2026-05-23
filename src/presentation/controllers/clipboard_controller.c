#include "clipboard_controller.h"
#include <glib.h>
#include <gio/gio.h>
#include <string.h>

struct _AetherClipboardController {
    char             *path;
    AetherClipboardOp op;
};

AetherClipboardController *aether_clipboard_controller_new(void) {
    AetherClipboardController *self = g_new0(AetherClipboardController, 1);
    self->op   = AETHER_CLIPBOARD_NONE;
    self->path = NULL;
    return self;
}

void aether_clipboard_controller_free(AetherClipboardController *self) {
    if (!self) return;
    g_free(self->path);
    g_free(self);
}

void aether_clipboard_set(AetherClipboardController *self, const char *path, AetherClipboardOp op) {
    g_free(self->path);
    self->path = g_strdup(path);
    self->op   = op;
}

gboolean aether_clipboard_has_content(AetherClipboardController *self) {
    return self->path != NULL && self->op != AETHER_CLIPBOARD_NONE;
}

AetherClipboardOp aether_clipboard_get_op(AetherClipboardController *self) {
    return self->op;
}

const char *aether_clipboard_get_path(AetherClipboardController *self) {
    return self->path;
}

/* ── Paste ── */

typedef struct {
    AetherClipboardController *ctrl;
    GTask *task;
} PasteData;

static void on_copy_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    PasteData *pd  = user_data;
    GError    *err = NULL;
    g_file_copy_finish(G_FILE(src), res, &err);
    if (err) {
        g_task_return_error(pd->task, err);
    } else {
        g_task_return_boolean(pd->task, TRUE);
    }
    g_object_unref(pd->task);
    g_free(pd);
}

static void on_move_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    PasteData *pd  = user_data;
    GError    *err = NULL;
    g_file_move_finish(G_FILE(src), res, &err);
    if (err) {
        g_task_return_error(pd->task, err);
    } else {
        /* clear clipboard after successful cut-paste */
        g_free(pd->ctrl->path);
        pd->ctrl->path = NULL;
        pd->ctrl->op   = AETHER_CLIPBOARD_NONE;
        g_task_return_boolean(pd->task, TRUE);
    }
    g_object_unref(pd->task);
    g_free(pd);
}

void aether_clipboard_paste(AetherClipboardController *self,
                             const char               *dest_dir,
                             GAsyncReadyCallback       cb,
                             gpointer                  user_data)
{
    if (!self->path || self->op == AETHER_CLIPBOARD_NONE) return;

    GTask *task = g_task_new(NULL, NULL, cb, user_data);
    PasteData *pd = g_new0(PasteData, 1);
    pd->ctrl = self;
    pd->task = g_object_ref(task);

    GFile *src      = g_file_new_for_path(self->path);
    char  *basename = g_path_get_basename(self->path);
    char  *dest_path = g_build_filename(dest_dir, basename, NULL);
    GFile *dest     = g_file_new_for_path(dest_path);

    if (self->op == AETHER_CLIPBOARD_COPY) {
        g_file_copy_async(src, dest,
                          G_FILE_COPY_NONE,
                          G_PRIORITY_DEFAULT,
                          NULL, NULL, NULL,
                          on_copy_done, pd);
    } else { /* CUT */
        g_file_move_async(src, dest,
                          G_FILE_COPY_NONE,
                          G_PRIORITY_DEFAULT,
                          NULL, NULL, NULL,
                          on_move_done, pd);
    }

    g_free(basename);
    g_free(dest_path);
    g_object_unref(src);
    g_object_unref(dest);
    g_object_unref(task);
}

void aether_clipboard_paste_finish(AetherClipboardController *self,
                                    GAsyncResult              *res,
                                    GError                   **error)
{
    g_task_propagate_boolean(G_TASK(res), error);
}
