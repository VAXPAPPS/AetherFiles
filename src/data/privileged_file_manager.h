#ifndef AETHER_PRIVILEGED_FILE_MANAGER_H
#define AETHER_PRIVILEGED_FILE_MANAGER_H

#include <gio/gio.h>
#include "../domain/file_entity.h"

G_BEGIN_DECLS

/**
 * aether_privileged_is_available:
 *
 * يتحقق من توفر pkexec والمساعد المثبّت.
 * يجب استدعاؤه قبل أي عملية محمية.
 *
 * Returns: TRUE إذا كان النظام جاهزاً لتنفيذ العمليات المحمية.
 */
gboolean aether_privileged_is_available(void);

/* ── إدراج مجلد محمي ───────────────────────────────────────────────── */

/**
 * aether_privileged_list_async:
 * @path: مسار المجلد المراد إدراجه
 * @cancellable: (nullable): كائن إلغاء اختياري
 * @callback: دالة الاستجابة
 * @user_data: بيانات المستخدم
 *
 * يُدرج محتوى المجلد المحمي بصلاحيات مرتفعة عبر pkexec.
 */
void aether_privileged_list_async(const char        *path,
                                   GCancellable      *cancellable,
                                   GAsyncReadyCallback callback,
                                   gpointer           user_data);

/**
 * aether_privileged_list_finish:
 *
 * Returns: (transfer full): GListModel من AetherFileEntity، أو NULL عند الخطأ.
 */
GListModel *aether_privileged_list_finish(GAsyncResult *result,
                                           GError      **error);

/* ── العمليات الكتابية المحمية ──────────────────────────────────────── */

void aether_privileged_copy_async(const char         *src,
                                   const char         *dst,
                                   GAsyncReadyCallback callback,
                                   gpointer            user_data);

void aether_privileged_move_async(const char         *src,
                                   const char         *dst,
                                   GAsyncReadyCallback callback,
                                   gpointer            user_data);

void aether_privileged_delete_async(const char         *path,
                                     GAsyncReadyCallback callback,
                                     gpointer            user_data);

void aether_privileged_mkdir_async(const char         *path,
                                    GAsyncReadyCallback callback,
                                    gpointer            user_data);

void aether_privileged_touch_async(const char         *path,
                                    GAsyncReadyCallback callback,
                                    gpointer            user_data);

void aether_privileged_compress_async(const char         *format,
                                       const char         *dest,
                                       GStrv               sources,
                                       GAsyncReadyCallback callback,
                                       gpointer            user_data);

void aether_privileged_extract_async(const char         *archive,
                                      const char         *dest,
                                      GAsyncReadyCallback callback,
                                      gpointer            user_data);

/**
 * aether_privileged_op_finish:
 *
 * تُكمل أي عملية كتابية محمية.
 * Returns: TRUE عند النجاح.
 */
gboolean aether_privileged_op_finish(GAsyncResult *result,
                                      GError      **error);

G_END_DECLS

#endif /* AETHER_PRIVILEGED_FILE_MANAGER_H */
