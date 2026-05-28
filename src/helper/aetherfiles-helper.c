/*
 * aetherfiles-helper.c
 *
 * برنامج مساعد يُشغَّل بصلاحيات root عبر pkexec/Polkit.
 * يُنفّذ عمليات الملفات المحمية ويُخرج نتيجة على stdout.
 *
 * الاستخدام:
 *   aetherfiles-helper list   <path>
 *   aetherfiles-helper copy   <src> <dst>
 *   aetherfiles-helper move   <src> <dst>
 *   aetherfiles-helper delete <path>
 *   aetherfiles-helper mkdir  <path>
 *   aetherfiles-helper touch  <path>
 *   aetherfiles-helper compress <format> <dest> <src1> [src2 ...]
 *   aetherfiles-helper extract  <archive> <dest>
 *
 * مخرجات list: سطر JSON لكل ملف:
 *   {"name":"...", "path":"...", "is_dir":true/false, "size":N, "icon":"..."}
 * مخرجات العمليات الأخرى:
 *   OK\n   أو   ERR:<message>\n
 */

#define _XOPEN_SOURCE 700
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

/* ── مساعدات إخراج ─────────────────────────────────────────────────── */

static void print_ok(void) {
    printf("OK\n");
    fflush(stdout);
}

static void print_err(const char *msg) {
    printf("ERR:%s\n", msg ? msg : "Unknown error");
    fflush(stdout);
}

/* يُهرّب مؤشرات JSON الخاصة بشكل بسيط */
static void json_escape_print(const char *s) {
    if (!s) { return; }
    for (; *s; s++) {
        switch (*s) {
        case '"':  printf("\\\""); break;
        case '\\': printf("\\\\"); break;
        case '\n': printf("\\n");  break;
        case '\r': printf("\\r");  break;
        case '\t': printf("\\t");  break;
        default:   printf("%c", *s); break;
        }
    }
}

/* ── تحديد أيقونة الملف ─────────────────────────────────────────────── */

static const char *guess_icon(const char *name, int is_dir) {
    if (is_dir) return "folder";
    if (!name) return "text-x-generic";

    const char *ext = strrchr(name, '.');
    if (!ext) return "text-x-generic";
    ext++;

    /* صور */
    if (!strcasecmp(ext, "png") || !strcasecmp(ext, "jpg") ||
        !strcasecmp(ext, "jpeg") || !strcasecmp(ext, "gif") ||
        !strcasecmp(ext, "bmp") || !strcasecmp(ext, "webp") ||
        !strcasecmp(ext, "svg") || !strcasecmp(ext, "tiff"))
        return "image-x-generic";

    /* فيديو */
    if (!strcasecmp(ext, "mp4") || !strcasecmp(ext, "mkv") ||
        !strcasecmp(ext, "webm") || !strcasecmp(ext, "avi") ||
        !strcasecmp(ext, "mov") || !strcasecmp(ext, "flv") ||
        !strcasecmp(ext, "wmv"))
        return "video-x-generic";

    /* صوت */
    if (!strcasecmp(ext, "mp3") || !strcasecmp(ext, "flac") ||
        !strcasecmp(ext, "ogg") || !strcasecmp(ext, "wav") ||
        !strcasecmp(ext, "aac") || !strcasecmp(ext, "m4a"))
        return "audio-x-generic";

    /* نصوص وبرمجة */
    if (!strcasecmp(ext, "txt") || !strcasecmp(ext, "md") ||
        !strcasecmp(ext, "rst"))
        return "text-x-generic";

    if (!strcasecmp(ext, "c") || !strcasecmp(ext, "h") ||
        !strcasecmp(ext, "cpp") || !strcasecmp(ext, "py") ||
        !strcasecmp(ext, "js") || !strcasecmp(ext, "ts") ||
        !strcasecmp(ext, "rs") || !strcasecmp(ext, "go"))
        return "text-x-script";

    /* ضغط */
    if (!strcasecmp(ext, "zip") || !strcasecmp(ext, "tar") ||
        !strcasecmp(ext, "gz") || !strcasecmp(ext, "xz") ||
        !strcasecmp(ext, "7z") || !strcasecmp(ext, "rar") ||
        !strcasecmp(ext, "bz2"))
        return "package-x-generic";

    /* PDF */
    if (!strcasecmp(ext, "pdf"))
        return "application-pdf";

    return "text-x-generic";
}

/* ── cmd: list ──────────────────────────────────────────────────────── */

static int cmd_list(const char *path) {
    DIR *d = opendir(path);
    if (!d) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Cannot open directory: %s", strerror(errno));
        print_err(buf);
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;

        /* بناء المسار الكامل */
        size_t plen = strlen(path);
        size_t nlen = strlen(ent->d_name);
        char *full = malloc(plen + nlen + 2);
        if (!full) continue;
        memcpy(full, path, plen);
        full[plen] = '/';
        memcpy(full + plen + 1, ent->d_name, nlen + 1);

        struct stat st;
        int is_dir = 0;
        long long size = 0;
        if (lstat(full, &st) == 0) {
            is_dir = S_ISDIR(st.st_mode);
            size   = (long long)st.st_size;
        }

        const char *icon = guess_icon(ent->d_name, is_dir);

        printf("{\"name\":\"");
        json_escape_print(ent->d_name);
        printf("\",\"path\":\"");
        json_escape_print(full);
        printf("\",\"is_dir\":%s,\"size\":%lld,\"icon\":\"",
               is_dir ? "true" : "false", size);
        json_escape_print(icon);
        printf("\"}\n");

        free(full);
    }
    closedir(d);
    fflush(stdout);
    return 0;
}

/* ── نسخ تعاودي ──────────────────────────────────────────────────────── */

static int copy_recursive(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st) != 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        if (mkdir(dst, st.st_mode & 0777) != 0 && errno != EEXIST)
            return -1;

        DIR *d = opendir(src);
        if (!d) return -1;

        struct dirent *ent;
        int ret = 0;
        while ((ent = readdir(d)) != NULL && ret == 0) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;

            size_t slen = strlen(src), nlen = strlen(ent->d_name);
            char *s2 = malloc(slen + nlen + 2);
            char *d2 = malloc(strlen(dst) + nlen + 2);
            if (!s2 || !d2) { free(s2); free(d2); ret = -1; break; }

            sprintf(s2, "%s/%s", src, ent->d_name);
            sprintf(d2, "%s/%s", dst, ent->d_name);
            ret = copy_recursive(s2, d2);
            free(s2); free(d2);
        }
        closedir(d);
        return ret;
    } else {
        /* نسخ ملف عادي */
        int in_fd  = open(src, O_RDONLY);
        if (in_fd < 0) return -1;
        int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 0666);
        if (out_fd < 0) { close(in_fd); return -1; }

        char buf[65536];
        ssize_t n;
        int ret = 0;
        while ((n = read(in_fd, buf, sizeof(buf))) > 0) {
            ssize_t written = 0;
            while (written < n) {
                ssize_t w = write(out_fd, buf + written, n - written);
                if (w < 0) { ret = -1; goto done_copy; }
                written += w;
            }
        }
        if (n < 0) ret = -1;
done_copy:
        close(in_fd);
        close(out_fd);
        return ret;
    }
}

/* ── حذف تعاودي ──────────────────────────────────────────────────────── */

static int delete_recursive(const char *path) {
    struct stat st;
    if (lstat(path, &st) != 0) return -1;

    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (!d) return -1;

        struct dirent *ent;
        int ret = 0;
        while ((ent = readdir(d)) != NULL && ret == 0) {
            if (!strcmp(ent->d_name, ".") || !strcmp(ent->d_name, ".."))
                continue;
            char *child = malloc(strlen(path) + strlen(ent->d_name) + 2);
            if (!child) { ret = -1; break; }
            sprintf(child, "%s/%s", path, ent->d_name);
            ret = delete_recursive(child);
            free(child);
        }
        closedir(d);
        if (ret == 0) ret = rmdir(path) == 0 ? 0 : -1;
        return ret;
    } else {
        return unlink(path) == 0 ? 0 : -1;
    }
}

/* ── cmd: copy ──────────────────────────────────────────────────────── */

static int cmd_copy(const char *src, const char *dst) {
    /* إذا كان المقصد مجلداً موجوداً، نضع الملف/المجلد داخله */
    char final_dst[4096];
    struct stat dst_st;
    if (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
        const char *basename = strrchr(src, '/');
        basename = basename ? basename + 1 : src;
        snprintf(final_dst, sizeof(final_dst), "%s/%s", dst, basename);
    } else {
        snprintf(final_dst, sizeof(final_dst), "%s", dst);
    }

    if (copy_recursive(src, final_dst) != 0) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Copy failed: %s", strerror(errno));
        print_err(buf);
        return 1;
    }
    print_ok();
    return 0;
}

/* ── cmd: move ──────────────────────────────────────────────────────── */

static int cmd_move(const char *src, const char *dst) {
    char final_dst[4096];
    struct stat dst_st;
    if (stat(dst, &dst_st) == 0 && S_ISDIR(dst_st.st_mode)) {
        const char *basename = strrchr(src, '/');
        basename = basename ? basename + 1 : src;
        snprintf(final_dst, sizeof(final_dst), "%s/%s", dst, basename);
    } else {
        snprintf(final_dst, sizeof(final_dst), "%s", dst);
    }

    /* حاول rename أولاً (أسرع إذا كانا في نفس الجهاز) */
    if (rename(src, final_dst) == 0) {
        print_ok();
        return 0;
    }

    /* إذا فشل (نظامان مختلفان)، انسخ ثم احذف */
    if (copy_recursive(src, final_dst) == 0 && delete_recursive(src) == 0) {
        print_ok();
        return 0;
    }

    char buf[512];
    snprintf(buf, sizeof(buf), "Move failed: %s", strerror(errno));
    print_err(buf);
    return 1;
}

/* ── cmd: delete ────────────────────────────────────────────────────── */

static int cmd_delete(const char *path) {
    if (delete_recursive(path) != 0) {
        char buf[512];
        snprintf(buf, sizeof(buf), "Delete failed: %s", strerror(errno));
        print_err(buf);
        return 1;
    }
    print_ok();
    return 0;
}

/* ── cmd: mkdir ─────────────────────────────────────────────────────── */

static int cmd_mkdir(const char *path) {
    /* نُنشئ بشكل تعاودي كل مستوى */
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);
    size_t len = strlen(tmp);

    for (size_t i = 1; i <= len; i++) {
        if (tmp[i] == '/' || tmp[i] == '\0') {
            char c = tmp[i]; tmp[i] = '\0';
            if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
                char buf[512];
                snprintf(buf, sizeof(buf), "mkdir failed at '%s': %s", tmp, strerror(errno));
                print_err(buf);
                return 1;
            }
            tmp[i] = c;
        }
    }
    print_ok();
    return 0;
}

/* ── cmd: touch ─────────────────────────────────────────────────────── */

static int cmd_touch(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0 && errno == EEXIST) {
        /* الملف موجود بالفعل - اكتفّ */
        print_ok();
        return 0;
    }
    if (fd < 0) {
        char buf[512];
        snprintf(buf, sizeof(buf), "touch failed: %s", strerror(errno));
        print_err(buf);
        return 1;
    }
    close(fd);
    print_ok();
    return 0;
}

/* ── cmd: compress ──────────────────────────────────────────────────── */

static int cmd_compress(int argc, char **argv) {
    /*
     * argv[0] = "compress"
     * argv[1] = format  (zip | tar.xz | 7z)
     * argv[2] = dest
     * argv[3..] = sources
     */
    if (argc < 4) {
        print_err("compress: insufficient arguments");
        return 1;
    }
    const char *format = argv[1];
    const char *dest   = argv[2];
    /* argv[3..argc-1] = sources */

    /* بناء الأمر */
    char cmd_buf[32768];
    int  off = 0;

    if (strcmp(format, "zip") == 0) {
        off += snprintf(cmd_buf + off, sizeof(cmd_buf) - off, "zip -rq \"%s\"", dest);
        for (int i = 3; i < argc; i++)
            off += snprintf(cmd_buf + off, sizeof(cmd_buf) - off, " \"%s\"", argv[i]);
    } else if (strcmp(format, "tar.xz") == 0) {
        off += snprintf(cmd_buf + off, sizeof(cmd_buf) - off, "tar -cJf \"%s\"", dest);
        for (int i = 3; i < argc; i++)
            off += snprintf(cmd_buf + off, sizeof(cmd_buf) - off, " \"%s\"", argv[i]);
    } else if (strcmp(format, "7z") == 0) {
        off += snprintf(cmd_buf + off, sizeof(cmd_buf) - off, "7z a -y \"%s\"", dest);
        for (int i = 3; i < argc; i++)
            off += snprintf(cmd_buf + off, sizeof(cmd_buf) - off, " \"%s\"", argv[i]);
    } else {
        print_err("Unsupported compression format");
        return 1;
    }

    int ret = system(cmd_buf);
    if (ret == 0) { print_ok(); return 0; }
    print_err("Compression failed");
    return 1;
}

/* ── cmd: extract ───────────────────────────────────────────────────── */

static int cmd_extract(const char *archive, const char *dest) {
    char cmd_buf[8192];

    if (strstr(archive, ".zip"))
        snprintf(cmd_buf, sizeof(cmd_buf), "unzip -q \"%s\" -d \"%s\"", archive, dest);
    else if (strstr(archive, ".tar"))
        snprintf(cmd_buf, sizeof(cmd_buf), "tar -xf \"%s\" -C \"%s\"", archive, dest);
    else if (strstr(archive, ".7z"))
        snprintf(cmd_buf, sizeof(cmd_buf), "7z x -y \"%s\" -o\"%s\"", archive, dest);
    else {
        print_err("Unsupported archive format");
        return 1;
    }

    int ret = system(cmd_buf);
    if (ret == 0) { print_ok(); return 0; }
    print_err("Extraction failed");
    return 1;
}

/* ── main ───────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: aetherfiles-helper <command> [args...]\n");
        return 1;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "list") == 0) {
        if (argc < 3) { print_err("list: missing path"); return 1; }
        return cmd_list(argv[2]);

    } else if (strcmp(cmd, "copy") == 0) {
        if (argc < 4) { print_err("copy: missing src or dst"); return 1; }
        return cmd_copy(argv[2], argv[3]);

    } else if (strcmp(cmd, "move") == 0) {
        if (argc < 4) { print_err("move: missing src or dst"); return 1; }
        return cmd_move(argv[2], argv[3]);

    } else if (strcmp(cmd, "delete") == 0) {
        if (argc < 3) { print_err("delete: missing path"); return 1; }
        return cmd_delete(argv[2]);

    } else if (strcmp(cmd, "mkdir") == 0) {
        if (argc < 3) { print_err("mkdir: missing path"); return 1; }
        return cmd_mkdir(argv[2]);

    } else if (strcmp(cmd, "touch") == 0) {
        if (argc < 3) { print_err("touch: missing path"); return 1; }
        return cmd_touch(argv[2]);

    } else if (strcmp(cmd, "compress") == 0) {
        /* argv: helper compress format dest src... */
        return cmd_compress(argc - 1, argv + 1);

    } else if (strcmp(cmd, "extract") == 0) {
        if (argc < 4) { print_err("extract: missing archive or dest"); return 1; }
        return cmd_extract(argv[2], argv[3]);

    } else {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        return 1;
    }
}
