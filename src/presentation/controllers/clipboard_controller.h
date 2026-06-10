#pragma once
#include <glib-object.h>
#include <gio/gio.h>

typedef enum {
    AETHER_CLIPBOARD_NONE,
    AETHER_CLIPBOARD_COPY,
    AETHER_CLIPBOARD_CUT,
} AetherClipboardOp;

typedef struct _AetherClipboardController AetherClipboardController;

AetherClipboardController *aether_clipboard_controller_new     (void);
void                       aether_clipboard_controller_free    (AetherClipboardController *self);

void              aether_clipboard_set              (AetherClipboardController *self, GStrv paths, AetherClipboardOp op);
void              aether_clipboard_paste            (AetherClipboardController *self, const char *dest_dir, GAsyncReadyCallback cb, gpointer user_data);
void              aether_clipboard_paste_with_flags (AetherClipboardController *self, const char *dest_dir, GFileCopyFlags flags, GAsyncReadyCallback cb, gpointer user_data);
void              aether_clipboard_paste_finish     (AetherClipboardController *self, GAsyncResult *res, GError **error);
gboolean          aether_clipboard_has_content      (AetherClipboardController *self);
AetherClipboardOp aether_clipboard_get_op           (AetherClipboardController *self);
GStrv             aether_clipboard_get_paths        (AetherClipboardController *self);

/* يرجع قائمة أسماء الملفات المتعارضة (موجودة في الوجهة) */
GPtrArray        *aether_clipboard_find_conflicts    (AetherClipboardController *self, const char *dest_dir);

/* ينسخ/ينقل مع تغيير أسماء الملفات المتعارضة تلقائياً */
void              aether_clipboard_paste_keep_both   (AetherClipboardController *self, const char *dest_dir);
