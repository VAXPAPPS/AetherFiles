/*
 * privileged_ops.c
 *
 * منطق العمليات المحمية — يُجمَّع مباشرةً داخل التطبيق الرئيسي.
 * يُستدعى عندما يُشغَّل التطبيق بوسيط --privileged عبر pkexec.
 *
 * الاستخدام (داخلي):
 *   pkexec aetherfiles --privileged list   <path>
 *   pkexec aetherfiles --privileged copy   <src> <dst>
 *   pkexec aetherfiles --privileged move   <src> <dst>
 *   pkexec aetherfiles --privileged delete <path>
 *   pkexec aetherfiles --privileged mkdir  <path>
 *   pkexec aetherfiles --privileged touch  <path>
 *   pkexec aetherfiles --privileged compress <fmt> <dst> <src...>
 *   pkexec aetherfiles --privileged extract  <archive> <dst>
 */

#define _GNU_SOURCE
#include "privileged_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

/* ── مخرجات ─────────────────────────────────────────────────────────── */

static void pok(void)  { puts("OK");  fflush(stdout); }
static void perr(const char *m) {
    printf("ERR:%s\n", m ? m : "Unknown"); fflush(stdout);
}

/* تهريب JSON بسيط */
static void jstr(const char *s) {
    if (!s) return;
    for (; *s; s++) {
        switch (*s) {
        case '"':  fputs("\\\"", stdout); break;
        case '\\': fputs("\\\\", stdout); break;
        case '\n': fputs("\\n",  stdout); break;
        case '\r': fputs("\\r",  stdout); break;
        case '\t': fputs("\\t",  stdout); break;
        default:   fputc(*s, stdout);    break;
        }
    }
}

/* ── اقتراح أيقونة ──────────────────────────────────────────────────── */

static const char *guess_icon(const char *name, int is_dir) {
    if (is_dir) return "folder";
    const char *e = strrchr(name, '.');
    if (!e) return "text-x-generic";
    e++;
    if (!strcasecmp(e,"png")||!strcasecmp(e,"jpg")||!strcasecmp(e,"jpeg")||
        !strcasecmp(e,"gif")||!strcasecmp(e,"bmp")||!strcasecmp(e,"svg")||
        !strcasecmp(e,"webp")) return "image-x-generic";
    if (!strcasecmp(e,"mp4")||!strcasecmp(e,"mkv")||!strcasecmp(e,"avi")||
        !strcasecmp(e,"mov")||!strcasecmp(e,"webm")) return "video-x-generic";
    if (!strcasecmp(e,"mp3")||!strcasecmp(e,"flac")||!strcasecmp(e,"ogg")||
        !strcasecmp(e,"wav")||!strcasecmp(e,"aac")) return "audio-x-generic";
    if (!strcasecmp(e,"zip")||!strcasecmp(e,"tar")||!strcasecmp(e,"gz")||
        !strcasecmp(e,"xz")||!strcasecmp(e,"7z")||!strcasecmp(e,"rar")||
        !strcasecmp(e,"bz2")) return "package-x-generic";
    if (!strcasecmp(e,"pdf")) return "application-pdf";
    if (!strcasecmp(e,"c")||!strcasecmp(e,"h")||!strcasecmp(e,"cpp")||
        !strcasecmp(e,"py")||!strcasecmp(e,"js")||!strcasecmp(e,"rs"))
        return "text-x-script";
    return "text-x-generic";
}

/* ── list ────────────────────────────────────────────────────────────── */

static int ops_list(const char *path) {
    DIR *d = opendir(path);
    if (!d) { perr(strerror(errno)); return 1; }

    struct dirent *ent;
    while ((ent = readdir(d))) {
        if (!strcmp(ent->d_name,".") || !strcmp(ent->d_name,"..")) continue;

        size_t plen = strlen(path), nlen = strlen(ent->d_name);
        char *full = malloc(plen + nlen + 2);
        if (!full) continue;
        memcpy(full, path, plen);
        full[plen] = '/';
        memcpy(full+plen+1, ent->d_name, nlen+1);

        struct stat st = {0};
        lstat(full, &st);
        int    is_dir = S_ISDIR(st.st_mode);
        long long sz  = (long long)st.st_size;
        const char *icon = guess_icon(ent->d_name, is_dir);

        printf("{\"name\":\""); jstr(ent->d_name);
        printf("\",\"path\":\""); jstr(full);
        printf("\",\"is_dir\":%s,\"size\":%lld,\"icon\":\"",
               is_dir?"true":"false", sz);
        jstr(icon);
        printf("\"}\n");
        free(full);
    }
    closedir(d);
    fflush(stdout);
    return 0;
}

/* ── نسخ تعاودي ──────────────────────────────────────────────────────── */

static int copy_r(const char *src, const char *dst) {
    struct stat st;
    if (lstat(src, &st)) return -1;

    if (S_ISDIR(st.st_mode)) {
        mkdir(dst, st.st_mode & 0777);
        DIR *d = opendir(src); if (!d) return -1;
        struct dirent *e; int ret = 0;
        while ((e = readdir(d)) && !ret) {
            if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
            char s2[4096], d2[4096];
            snprintf(s2,sizeof(s2),"%s/%s",src,e->d_name);
            snprintf(d2,sizeof(d2),"%s/%s",dst,e->d_name);
            ret = copy_r(s2, d2);
        }
        closedir(d); return ret;
    }

    int in  = open(src, O_RDONLY);
    if (in < 0) return -1;
    int out = open(dst, O_WRONLY|O_CREAT|O_TRUNC, st.st_mode & 0666);
    if (out < 0) { close(in); return -1; }
    char buf[65536]; ssize_t n; int ret = 0;
    while ((n = read(in,buf,sizeof(buf))) > 0) {
        ssize_t w = 0;
        while (w < n) { ssize_t x = write(out,buf+w,n-w); if(x<0){ret=-1;goto done;} w+=x; }
    }
    if (n < 0) ret = -1;
done: close(in); close(out); return ret;
}

/* ── حذف تعاودي ──────────────────────────────────────────────────────── */

static int del_r(const char *path) {
    struct stat st;
    if (lstat(path,&st)) return -1;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path); if (!d) return -1;
        struct dirent *e; int ret = 0;
        while ((e = readdir(d)) && !ret) {
            if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
            char child[4096];
            snprintf(child,sizeof(child),"%s/%s",path,e->d_name);
            ret = del_r(child);
        }
        closedir(d);
        return ret ? ret : (rmdir(path) ? -1 : 0);
    }
    return unlink(path) ? -1 : 0;
}

/* ── mkdir متعدد المستويات ───────────────────────────────────────────── */

static int ops_mkdir_p(const char *path) {
    char tmp[4096]; snprintf(tmp,sizeof(tmp),"%s",path);
    for (size_t i = 1; i <= strlen(tmp); i++) {
        if (tmp[i]=='/' || tmp[i]=='\0') {
            char c=tmp[i]; tmp[i]='\0';
            if (mkdir(tmp,0755) && errno!=EEXIST) return -1;
            tmp[i]=c;
        }
    }
    return 0;
}

/* ── copy ────────────────────────────────────────────────────────────── */

static int ops_copy(const char *src, const char *dst) {
    char final[4096];
    struct stat s;
    if (!stat(dst,&s) && S_ISDIR(s.st_mode)) {
        const char *b = strrchr(src,'/'); b = b?b+1:src;
        snprintf(final,sizeof(final),"%s/%s",dst,b);
    } else snprintf(final,sizeof(final),"%s",dst);

    if (copy_r(src,final)) { perr(strerror(errno)); return 1; }
    pok(); return 0;
}

/* ── move ────────────────────────────────────────────────────────────── */

static int ops_move(const char *src, const char *dst) {
    char final[4096];
    struct stat s;
    if (!stat(dst,&s) && S_ISDIR(s.st_mode)) {
        const char *b = strrchr(src,'/'); b = b?b+1:src;
        snprintf(final,sizeof(final),"%s/%s",dst,b);
    } else snprintf(final,sizeof(final),"%s",dst);

    if (rename(src,final)==0) { pok(); return 0; }
    if (copy_r(src,final)==0 && del_r(src)==0) { pok(); return 0; }
    perr(strerror(errno)); return 1;
}

/* ── delete ──────────────────────────────────────────────────────────── */

static int ops_delete(const char *path) {
    if (del_r(path)) { perr(strerror(errno)); return 1; }
    pok(); return 0;
}

/* ── mkdir ───────────────────────────────────────────────────────────── */

static int ops_mkdir(const char *path) {
    if (ops_mkdir_p(path)) { perr(strerror(errno)); return 1; }
    pok(); return 0;
}

/* ── touch ───────────────────────────────────────────────────────────── */

static int ops_touch(const char *path) {
    int fd = open(path, O_WRONLY|O_CREAT|O_EXCL, 0644);
    if (fd < 0 && errno != EEXIST) { perr(strerror(errno)); return 1; }
    if (fd >= 0) close(fd);
    pok(); return 0;
}

/* ── compress ────────────────────────────────────────────────────────── */

static int ops_compress(int argc, char **argv) {
    /* argv[0]=compress argv[1]=fmt argv[2]=dst argv[3..]=src */
    if (argc < 4) { perr("compress: insufficient arguments"); return 1; }
    const char *fmt = argv[1], *dst = argv[2];
    char cmd[32768]; int off = 0;
    if (!strcmp(fmt,"zip")) {
        off += snprintf(cmd+off,sizeof(cmd)-off,"zip -rq \"%s\"",dst);
    } else if (!strcmp(fmt,"tar.xz")) {
        off += snprintf(cmd+off,sizeof(cmd)-off,"tar -cJf \"%s\"",dst);
    } else if (!strcmp(fmt,"7z")) {
        off += snprintf(cmd+off,sizeof(cmd)-off,"7z a -y \"%s\"",dst);
    } else { perr("Unsupported format"); return 1; }
    for (int i=3;i<argc;i++)
        off += snprintf(cmd+off,sizeof(cmd)-off," \"%s\"",argv[i]);
    if (system(cmd)) { perr("Compression failed"); return 1; }
    pok(); return 0;
}

/* ── extract ─────────────────────────────────────────────────────────── */

static int ops_extract(const char *archive, const char *dst) {
    char cmd[8192];
    if (strstr(archive,".zip"))
        snprintf(cmd,sizeof(cmd),"unzip -q \"%s\" -d \"%s\"",archive,dst);
    else if (strstr(archive,".tar"))
        snprintf(cmd,sizeof(cmd),"tar -xf \"%s\" -C \"%s\"",archive,dst);
    else if (strstr(archive,".7z"))
        snprintf(cmd,sizeof(cmd),"7z x -y \"%s\" -o\"%s\"",archive,dst);
    else { perr("Unsupported archive format"); return 1; }
    if (system(cmd)) { perr("Extraction failed"); return 1; }
    pok(); return 0;
}

/* ── نقطة الدخول الرئيسية ─────────────────────────────────────────── */

int privileged_ops_run(int argc, char **argv) {
    /*
     * argv[0] = "--privileged"   (تم التحقق منه في main)
     * argv[1] = العملية          (list / copy / move / ...)
     * argv[2..] = الوسائط
     */
    if (argc < 2) { fprintf(stderr,"Missing operation\n"); return 1; }
    const char *op = argv[1];

    if (!strcmp(op,"list"))   { if(argc<3){perr("list: missing path");return 1;} return ops_list(argv[2]); }
    if (!strcmp(op,"copy"))   { if(argc<4){perr("copy: missing args");return 1;} return ops_copy(argv[2],argv[3]); }
    if (!strcmp(op,"move"))   { if(argc<4){perr("move: missing args");return 1;} return ops_move(argv[2],argv[3]); }
    if (!strcmp(op,"delete")) { if(argc<3){perr("delete: missing path");return 1;} return ops_delete(argv[2]); }
    if (!strcmp(op,"mkdir"))  { if(argc<3){perr("mkdir: missing path");return 1;} return ops_mkdir(argv[2]); }
    if (!strcmp(op,"touch"))  { if(argc<3){perr("touch: missing path");return 1;} return ops_touch(argv[2]); }
    if (!strcmp(op,"compress")) return ops_compress(argc-1, argv+1);
    if (!strcmp(op,"extract"))  { if(argc<4){perr("extract: missing args");return 1;} return ops_extract(argv[2],argv[3]); }

    fprintf(stderr,"Unknown operation: %s\n", op);
    return 1;
}

/* ── وضع الـ daemon ─────────────────────────────────────────────────── */
/*
 * بروتوكول stdin/stdout:
 *   الإرسال:  <op>\t<arg1>\t<arg2>...\n
 *   الاستقبال: OK\n  أو  ERR:<msg>\n  أو سطر JSON لكل ملف (list)
 *   الإنهاء:  EOF على stdin أو سطر "quit\n"
 *
 * مثال:
 *   list\t/root\n        → أسطر JSON ثم DONE\n
 *   copy\t/src\t/dst\n   → OK\n
 *   quit\n               → إنهاء
 */

/* تقسيم سطر بـ \t إلى مصفوفة وسائط — المُستدعي يُحرّر argv[] */
static int split_tab_line(char *line, char **argv, int max_argc) {
    int argc = 0;
    char *p = line;
    while (*p && argc < max_argc - 1) {
        argv[argc++] = p;
        char *tab = strchr(p, '\t');
        if (!tab) break;
        *tab = '\0';
        p = tab + 1;
    }
    /* الوسيط الأخير إلى نهاية السطر */
    if (*p && argc < max_argc - 1) argv[argc++] = p;
    argv[argc] = NULL;
    return argc;
}

int privileged_ops_run_daemon(void) {
    /* أعلن الجاهزية للطرف الآخر */
    puts("READY");
    fflush(stdout);

    char line[65536];
    while (fgets(line, sizeof(line), stdin)) {
        /* إزالة \n في النهاية */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
        if (len > 1 && line[len-2] == '\r') line[len-2] = '\0';

        if (!line[0]) continue;              /* سطر فارغ */
        if (!strcmp(line, "quit")) break;    /* إنهاء نظيف */

        /* تقسيم السطر إلى أوامر */
        char *argv[256];
        int argc = split_tab_line(line, argv, 256);
        if (argc < 1) continue;

        const char *op = argv[0];

        if (!strcmp(op, "list")) {
            if (argc < 2) { perr("list: missing path"); continue; }
            ops_list(argv[1]);
            puts("DONE"); fflush(stdout);
        } else if (!strcmp(op, "copy")) {
            if (argc < 3) { perr("copy: missing args"); continue; }
            ops_copy(argv[1], argv[2]);
        } else if (!strcmp(op, "move")) {
            if (argc < 3) { perr("move: missing args"); continue; }
            ops_move(argv[1], argv[2]);
        } else if (!strcmp(op, "delete")) {
            if (argc < 2) { perr("delete: missing path"); continue; }
            ops_delete(argv[1]);
        } else if (!strcmp(op, "mkdir")) {
            if (argc < 2) { perr("mkdir: missing path"); continue; }
            ops_mkdir(argv[1]);
        } else if (!strcmp(op, "touch")) {
            if (argc < 2) { perr("touch: missing path"); continue; }
            ops_touch(argv[1]);
        } else if (!strcmp(op, "compress")) {
            /* compress\tfmt\tdst\tsrc1\tsrc2... */
            /* نبني argv مُعاد التنسيق: argv[0]="compress" argv[1]=fmt ... */
            ops_compress(argc, argv);
        } else if (!strcmp(op, "extract")) {
            if (argc < 3) { perr("extract: missing args"); continue; }
            ops_extract(argv[1], argv[2]);
        } else {
            perr("Unknown operation");
        }
    }
    return 0;
}

