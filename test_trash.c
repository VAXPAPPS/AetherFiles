#include <gio/gio.h>

void ready(GObject *s, GAsyncResult *res, gpointer d) {
    GError *err = NULL;
    GFileEnumerator *e = g_file_enumerate_children_finish(G_FILE(s), res, &err);
    if (err) { g_printerr("enum err: %s\n", err->message); return; }
    
    while(1) {
        GError *e2 = NULL;
        GFileInfo *info = g_file_enumerator_next_file(e, NULL, &e2);
        if (e2) g_printerr("next err: %s\n", e2->message);
        if (!info) break;
        g_printerr("Got: %s\n", g_file_info_get_name(info));
    }
    g_main_loop_quit(d);
}

int main() {
    GMainLoop *l = g_main_loop_new(NULL, 0);
    GFile *f = g_file_parse_name("trash:///");
    g_file_enumerate_children_async(f, "standard::name", 0, 0, NULL, ready, l);
    g_main_loop_run(l);
    return 0;
}
