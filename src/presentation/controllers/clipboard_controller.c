#include "clipboard_controller.h"
#include <glib.h>
#include <gio/gio.h>
#include <string.h>
#include "../../data/privileged_file_manager.h"

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
    g_return_val_if_fail(self != NULL, NULL);
    return self->paths; /* يُفضل إرجاع نسخة إذا لزم الأمر، لكن حالياً نرجع المؤشر للسهولة */
}

gboolean aether_clipboard_is_same_location(AetherClipboardController *self, const char *dest_dir) {
    g_return_val_if_fail(self != NULL, FALSE);
    if (!self->paths || !dest_dir) return FALSE;
    for (int i = 0; self->paths[i]; i++) {
        char *parent = g_path_get_dirname(self->paths[i]);
        gboolean same = (g_strcmp0(parent, dest_dir) == 0);
        g_free(parent);
        if (!same) return FALSE;
    }
    return TRUE;
}

gboolean aether_clipboard_validate_sources(AetherClipboardController *self, char **out_missing_msg) {
    g_return_val_if_fail(self != NULL, FALSE);
    if (!self->paths || !self->paths[0]) return TRUE; /* Empty is technically valid/no missing sources */

    GString *missing = g_string_new("");
    int missing_count = 0;

    for (int i = 0; self->paths[i]; i++) {
        if (!g_file_test(self->paths[i], G_FILE_TEST_EXISTS)) {
            char *basename = g_path_get_basename(self->paths[i]);
            if (missing_count > 0) g_string_append(missing, "\n");
            g_string_append_printf(missing, "• %s", basename);
            g_free(basename);
            missing_count++;
        }
    }

    if (missing_count > 0) {
        if (out_missing_msg) {
            *out_missing_msg = g_strdup_printf(
                "Cannot complete operation. %d source item%s no longer exist:\n\n%s",
                missing_count, missing_count > 1 ? "s" : "", missing->str
            );
        }
        g_string_free(missing, TRUE);
        
        /* EC-03: مسح الحافظة لأنها تحتوي ملفات محذوفة */
        aether_clipboard_set(self, NULL, AETHER_CLIPBOARD_NONE);
        return FALSE;
    }

    g_string_free(missing, TRUE);
    if (out_missing_msg) *out_missing_msg = NULL;
    return TRUE;
}

/* ── Forward Declarations ── */
static gboolean merge_copy_recursive(GFile *src, GFile *dst, gboolean do_move, GError **error);

/* ── Paste ── */

typedef struct {
    AetherClipboardController *ctrl;
    GTask                     *task;
    int                        pending;
    int                        total;
    gboolean                   has_error;
    GError                    *first_error;
    char                      *dest_dir;
} PasteData;

typedef struct {
    PasteData *pd;
    GFile     *src;
    GFile     *dest;
} PasteItemData;

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
        g_free(pd->dest_dir);
        g_object_unref(pd->task);
        g_free(pd);
    }
}


static void on_priv_op_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    (void)src;
    PasteItemData *item = user_data;
    PasteData     *pd   = item->pd;
    GError        *err  = NULL;
    if (!aether_privileged_op_finish(res, &err)) {
        pd->has_error = TRUE;
        if (!pd->first_error) pd->first_error = err;
        else g_error_free(err);
    }
    g_object_unref(item->src);
    g_object_unref(item->dest);
    g_free(item);
    check_paste_completion(pd);
}

static void on_copy_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    PasteItemData *item = user_data;
    PasteData     *pd   = item->pd;
    GError        *err  = NULL;
    g_file_copy_finish(G_FILE(src), res, &err);
    if (err) {
        if (err->code == G_IO_ERROR_WOULD_RECURSE) {
            g_error_free(err);
            GError *merge_err = NULL;
            if (!merge_copy_recursive(item->src, item->dest, FALSE, &merge_err)) {
                if (merge_err && merge_err->code == G_IO_ERROR_PERMISSION_DENIED && aether_privileged_is_available()) {
                    g_error_free(merge_err);
                    if (!aether_privileged_daemon_is_running()) aether_privileged_daemon_start();
                    char *sp = g_file_get_path(item->src);
                    char *dp = g_file_get_path(item->dest);
                    aether_privileged_copy_async(sp, dp, on_priv_op_done, item);
                    g_free(sp); g_free(dp);
                    return;
                }
                pd->has_error = TRUE;
                if (!pd->first_error) pd->first_error = merge_err;
                else if (merge_err) g_error_free(merge_err);
            }
        } else if (err->code == G_IO_ERROR_PERMISSION_DENIED && aether_privileged_is_available()) {
            g_error_free(err);
            if (!aether_privileged_daemon_is_running()) aether_privileged_daemon_start();
            char *sp = g_file_get_path(item->src);
            char *dp = g_file_get_path(item->dest);
            aether_privileged_copy_async(sp, dp, on_priv_op_done, item);
            g_free(sp); g_free(dp);
            return;
        } else {
            pd->has_error = TRUE;
            if (!pd->first_error) pd->first_error = err;
            else g_error_free(err);
        }
    }
    g_object_unref(item->src);
    g_object_unref(item->dest);
    g_free(item);
    check_paste_completion(pd);
}

static void on_move_done(GObject *src, GAsyncResult *res, gpointer user_data) {
    PasteItemData *item = user_data;
    PasteData     *pd   = item->pd;
    GError        *err  = NULL;
    g_file_move_finish(G_FILE(src), res, &err);
    
    if (err) {
        /* ── EC-10: Fallback to copy+delete for cross-filesystem move of directories ── */
        if (err->code == G_IO_ERROR_NOT_SUPPORTED || err->code == G_IO_ERROR_WOULD_MERGE) {
            g_printerr("Cross-device move or merge detected. Falling back to merge_copy_recursive.\n");
            g_error_free(err);
            
            GError *merge_err = NULL;
            if (!merge_copy_recursive(item->src, item->dest, TRUE, &merge_err)) {
                if (merge_err && merge_err->code == G_IO_ERROR_PERMISSION_DENIED && aether_privileged_is_available()) {
                    g_error_free(merge_err);
                    if (!aether_privileged_daemon_is_running()) aether_privileged_daemon_start();
                    char *sp = g_file_get_path(item->src);
                    char *dp = g_file_get_path(item->dest);
                    aether_privileged_move_async(sp, dp, on_priv_op_done, item);
                    g_free(sp); g_free(dp);
                    return;
                }
                pd->has_error = TRUE;
                if (!pd->first_error) pd->first_error = merge_err;
                else if (merge_err) g_error_free(merge_err);
            }
        } else if (err->code == G_IO_ERROR_PERMISSION_DENIED && aether_privileged_is_available()) {
            g_error_free(err);
            if (!aether_privileged_daemon_is_running()) aether_privileged_daemon_start();
            char *sp = g_file_get_path(item->src);
            char *dp = g_file_get_path(item->dest);
            aether_privileged_move_async(sp, dp, on_priv_op_done, item);
            g_free(sp); g_free(dp);
            return;
        } else {
            pd->has_error = TRUE;
            if (!pd->first_error) pd->first_error = err;
            else g_error_free(err);
        }
    }
    
    g_object_unref(item->src);
    g_object_unref(item->dest);
    g_free(item);
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
    /* EC-11: منع الـ loop في النسخ/النقل */
    flags |= G_FILE_COPY_NOFOLLOW_SYMLINKS;
    
    if (!self->paths || !self->paths[0] || self->op == AETHER_CLIPBOARD_NONE) return;

    GTask *task = g_task_new(NULL, NULL, cb, user_data);
    PasteData *pd = g_new0(PasteData, 1);
    pd->ctrl = self;
    pd->task = g_object_ref(task);

    pd->total   = g_strv_length(self->paths);
    pd->pending = pd->total;
    pd->has_error  = FALSE;
    pd->first_error = NULL;

    pd->dest_dir = g_strdup(dest_dir);

    for (int i = 0; i < pd->total; i++) {
        GFile *src      = g_file_new_for_path(self->paths[i]);
        char  *basename = g_path_get_basename(self->paths[i]);
        char  *dest_path = g_build_filename(dest_dir, basename, NULL);
        GFile *dest     = g_file_new_for_path(dest_path);

        PasteItemData *item = g_new0(PasteItemData, 1);
        item->pd   = pd;
        item->src  = g_object_ref(src);
        item->dest = g_object_ref(dest);

        if (self->op == AETHER_CLIPBOARD_COPY) {
            g_file_copy_async(src, dest,
                              flags,
                              G_PRIORITY_DEFAULT,
                              NULL, NULL, NULL,
                              on_copy_done, item);
        } else { /* CUT */
            g_file_move_async(src, dest,
                              flags,
                              G_PRIORITY_DEFAULT,
                              NULL, NULL, NULL,
                              on_move_done, item);
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
            if (err->code == G_IO_ERROR_PERMISSION_DENIED && aether_privileged_is_available()) {
                g_error_free(err);
                if (!aether_privileged_daemon_is_running()) aether_privileged_daemon_start();
                if (self->op == AETHER_CLIPBOARD_COPY)
                    aether_privileged_copy_async(self->paths[i], dest_path, NULL, NULL);
                else
                    aether_privileged_move_async(self->paths[i], dest_path, NULL, NULL);
            } else {
                g_printerr("Keep-both error for %s: %s\n", basename, err->message);
                g_error_free(err);
            }
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

/* ══════════════════════════════════════════════════════════════════
 * Paste Merge: دمج ذكي يحافظ على ملفات الوجهة غير الموجودة في المصدر
 * ══════════════════════════════════════════════════════════════════ */

/*
 * دمج تعاودي: ينسخ كل محتوى src إلى dst مع دمج المجلدات بشكل ذكي.
 * - إذا كان src مجلداً ودst مجلداً موجوداً: ادمج المحتوى تعاودياً
 * - إذا كان src ملفاً: استبدله في الوجهة
 * - إذا لم يكن dst موجوداً: انسخ مباشرةً
 *
 * العملية: COPY (نسخ) أو MOVE (نقل وحذف المصدر بعد النسخ)
 */
static gboolean merge_copy_recursive(GFile *src, GFile *dst, gboolean do_move, GError **error) {
    GFileType src_type = g_file_query_file_type(src,
                             G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);
    GFileType dst_type = g_file_query_file_type(dst,
                             G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL);

    if (src_type == G_FILE_TYPE_DIRECTORY) {
        /* المصدر مجلد */
        if (dst_type == G_FILE_TYPE_DIRECTORY) {
            /* ── EC-13: نقل مجلد للقراءة فقط → منح المالك صلاحية كتابة للتمكن من الدمج ── */
            GFileInfo *dst_info = g_file_query_info(dst, G_FILE_ATTRIBUTE_UNIX_MODE, 0, NULL, NULL);
            if (dst_info) {
                if (g_file_info_has_attribute(dst_info, G_FILE_ATTRIBUTE_UNIX_MODE)) {
                    guint32 mode = g_file_info_get_attribute_uint32(dst_info, G_FILE_ATTRIBUTE_UNIX_MODE);
                    if (!(mode & 0200)) { /* لا يملك صلاحية الكتابة */
                        g_file_set_attribute_uint32(dst, G_FILE_ATTRIBUTE_UNIX_MODE, mode | 0200,
                                                    G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, NULL, NULL);
                    }
                }
                g_object_unref(dst_info);
            }

            /* الوجهة أيضاً مجلد: ادمج المحتوى تعاودياً */
            GError *err = NULL;
            GFileEnumerator *en = g_file_enumerate_children(
                src,
                G_FILE_ATTRIBUTE_STANDARD_NAME,
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                NULL, &err);
            if (!en) {
                if (err && err->code == G_IO_ERROR_PERMISSION_DENIED) {
                    g_propagate_error(error, err);
                    return FALSE;
                }
                if (err) {
                    g_printerr("merge_copy_recursive: cannot enumerate %s: %s\n",
                               g_file_peek_path(src), err->message);
                    g_error_free(err);
                }
                return TRUE;
            }
            GFileInfo *info;
            while ((info = g_file_enumerator_next_file(en, NULL, NULL)) != NULL) {
                const char *child_name = g_file_info_get_name(info);
                GFile *child_src = g_file_get_child(src, child_name);
                GFile *child_dst = g_file_get_child(dst, child_name);
                if (!merge_copy_recursive(child_src, child_dst, do_move, error)) { g_object_unref(child_src); g_object_unref(child_dst); g_object_unref(info); g_object_unref(en); return FALSE; }
                g_object_unref(child_src);
                g_object_unref(child_dst);
                g_object_unref(info);
            }
            g_object_unref(en);

            /* إذا كان نقلاً: احذف المجلد المصدر الفارغ */
            if (do_move) {
                g_file_delete(src, NULL, NULL); /* يفشل بصمت إذا لم يكن فارغاً */
            }
        } else {
            /*
             * الوجهة غير موجودة (UNKNOWN) أو ملف عادي.
             * نحذف الملف أولاً إن وُجد.
             */
            if (dst_type != G_FILE_TYPE_UNKNOWN)
                g_file_delete(dst, NULL, NULL);

            if (do_move) {
                /*
                 * نقل مجلد: جرّب rename أولاً (سريع — نفس جهاز التخزين).
                 * G_FILE_COPY_NO_FALLBACK_FOR_MOVE يمنع GIO من نسخ+حذف
                 * تلقائياً حتى نتمكن من معالجة الحالات بأنفسنا.
                 */
                GError *mv_err = NULL;
                if (g_file_move(src, dst,
                                G_FILE_COPY_NO_FALLBACK_FOR_MOVE,
                                NULL, NULL, NULL, &mv_err)) {
                    return TRUE; /* rename نجح */
                }
                if (mv_err && mv_err->code == G_IO_ERROR_PERMISSION_DENIED) {
                    g_propagate_error(error, mv_err);
                    return FALSE;
                }
                g_clear_error(&mv_err);
                /*
                 * rename فشل (أجهزة مختلفة أو VFS) — أنشئ مجلد
                 * الوجهة وانقل الأبناء يدوياً بشكل تعاودي.
                 */
            }

            /* أنشئ مجلد الوجهة */
            GError *mk_err = NULL;
            if (!g_file_make_directory(dst, NULL, &mk_err)) {
                if (mk_err && mk_err->code == G_IO_ERROR_PERMISSION_DENIED) {
                    g_propagate_error(error, mk_err);
                    return FALSE;
                }
                g_printerr("merge: g_file_make_directory(%s) failed: %s\n",
                           g_file_peek_path(dst),
                           mk_err ? mk_err->message : "?");
                g_clear_error(&mk_err);
                return TRUE;
            }

            /* dst أصبح الآن مجلداً — ادمج الأبناء بنفس المنطق أعلاه */
            GError *en_err2 = NULL;
            GFileEnumerator *en2 = g_file_enumerate_children(
                src, G_FILE_ATTRIBUTE_STANDARD_NAME,
                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                NULL, &en_err2);
            if (!en2) {
                if (en_err2 && en_err2->code == G_IO_ERROR_PERMISSION_DENIED) {
                    g_propagate_error(error, en_err2);
                    return FALSE;
                }
                g_printerr("merge: enumerate %s failed: %s\n",
                           g_file_peek_path(src),
                           en_err2 ? en_err2->message : "?");
                g_clear_error(&en_err2);
                return TRUE;
            }
            GFileInfo *info2;
            while ((info2 = g_file_enumerator_next_file(en2, NULL, NULL)) != NULL) {
                const char *child_name = g_file_info_get_name(info2);
                GFile *child_src = g_file_get_child(src, child_name);
                GFile *child_dst = g_file_get_child(dst, child_name);
                if (!merge_copy_recursive(child_src, child_dst, do_move, error)) { g_object_unref(child_src); g_object_unref(child_dst); g_object_unref(info2); g_object_unref(en2); return FALSE; }
                g_object_unref(child_src);
                g_object_unref(child_dst);
                g_object_unref(info2);
            }
            g_object_unref(en2);

            if (do_move)
                g_file_delete(src, NULL, NULL);
        }
        return TRUE;
    } else {
        /* المصدر ملف عادي أو رابط رمزي (Symlink): استبدله في الوجهة دائماً */
        GError *err = NULL;
        /* EC-11: منع اتباع الروابط الرمزية بنسخ الرابط نفسه بدلاً من هدفه */
        GFileCopyFlags flags = G_FILE_COPY_OVERWRITE | G_FILE_COPY_NOFOLLOW_SYMLINKS;
        if (do_move) {
            if (!g_file_move(src, dst, flags, NULL, NULL, NULL, &err)) {
                if (err) {
                    if (err->code == G_IO_ERROR_PERMISSION_DENIED) {
                        g_propagate_error(error, err);
                        return FALSE;
                    }
                    /* ── EC-07: معالجة خطأ القرص الممتلئ ── */
                    if (err->code == G_IO_ERROR_NO_SPACE) {
                        g_printerr("Disk full! Deleting partial file: %s\n", g_file_peek_path(dst));
                        g_file_delete(dst, NULL, NULL); /* حذف الملف الجزئي/المشوه */
                    } else {
                        g_printerr("merge move file error: %s\n", err->message);
                    }
                    g_error_free(err);
                }
            }
        } else {
            if (!g_file_copy(src, dst, flags, NULL, NULL, NULL, &err)) {
                if (err) {
                    if (err->code == G_IO_ERROR_PERMISSION_DENIED) {
                        g_propagate_error(error, err);
                        return FALSE;
                    }
                    /* ── EC-07: معالجة خطأ القرص الممتلئ ── */
                    if (err->code == G_IO_ERROR_NO_SPACE) {
                        g_printerr("Disk full! Deleting partial file: %s\n", g_file_peek_path(dst));
                        g_file_delete(dst, NULL, NULL); /* حذف الملف الجزئي/المشوه */
                    } else {
                        g_printerr("merge copy file error: %s\n", err->message);
                    }
                    g_error_free(err);
                }
            }
        }
    }
    return TRUE;
}

void aether_clipboard_paste_merge(AetherClipboardController *self,
                                   const char               *dest_dir)
{
    if (!self->paths) return;

    gboolean do_move = (self->op == AETHER_CLIPBOARD_CUT);

    for (int i = 0; self->paths[i]; i++) {
        GFile *src      = g_file_new_for_path(self->paths[i]);
        char  *basename = g_path_get_basename(self->paths[i]);
        char  *dst_path = g_build_filename(dest_dir, basename, NULL);
        GFile *dst      = g_file_new_for_path(dst_path);

        GError *err = NULL;
        if (!merge_copy_recursive(src, dst, do_move, &err)) {
            if (err && err->code == G_IO_ERROR_PERMISSION_DENIED && aether_privileged_is_available()) {
                g_error_free(err);
                if (!aether_privileged_daemon_is_running()) aether_privileged_daemon_start();
                if (do_move)
                    aether_privileged_move_async(self->paths[i], dst_path, NULL, NULL);
                else
                    aether_privileged_copy_async(self->paths[i], dst_path, NULL, NULL);
            } else if (err) {
                g_printerr("Merge copy failed: %s\n", err->message);
                g_error_free(err);
            }
        }

        g_object_unref(src);
        g_object_unref(dst);
        g_free(dst_path);
        g_free(basename);
    }

    /* مسح الـ clipboard عند القص */
    if (self->op == AETHER_CLIPBOARD_CUT) {
        g_strfreev(self->paths);
        self->paths = NULL;
        self->op    = AETHER_CLIPBOARD_NONE;
    }
}

