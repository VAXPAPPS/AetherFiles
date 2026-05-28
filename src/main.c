#include "presentation/application.h"
#include "helper/privileged_ops.h"
#include <string.h>

int main(int argc, char *argv[]) {
    /* وضع المساعد المحمي (أمر واحد) — يُستدعى عبر pkexec */
    if (argc >= 2 && strcmp(argv[1], "--privileged") == 0)
        return privileged_ops_run(argc - 1, argv + 1);

    /* وضع الـ daemon المحمي (يبقى حياً طوال الجلسة) — يُستدعى عبر pkexec */
    if (argc >= 2 && strcmp(argv[1], "--privileged-daemon") == 0)
        return privileged_ops_run_daemon();

    AetherApplication *app = aether_application_new();
    int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}

