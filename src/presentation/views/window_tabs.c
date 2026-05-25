#include "window_private.h"
#include <glib/gi18n.h>

void tab_session_free_fields(AetherTabSession *s) {
    g_free(s->path);
    if (s->back) g_ptr_array_free(s->back, TRUE);
    if (s->fwd)  g_ptr_array_free(s->fwd,  TRUE);
}

void tab_session_save(AetherWindow *self, int idx) {
    if (!self->tabs || idx < 0 || (guint)idx >= self->tabs->len) return;
    AetherTabSession *s = &g_array_index(self->tabs, AetherTabSession, idx);
    g_free(s->path);
    s->path = g_strdup(self->current_path);
    if (s->back) g_ptr_array_free(s->back, TRUE);
    if (s->fwd)  g_ptr_array_free(s->fwd,  TRUE);
    /* Clone stacks */
    s->back = g_ptr_array_new_with_free_func(g_free);
    s->fwd  = g_ptr_array_new_with_free_func(g_free);
    for (guint i = 0; i < self->back_stack->len; i++)
        g_ptr_array_add(s->back, g_strdup(self->back_stack->pdata[i]));
    for (guint i = 0; i < self->fwd_stack->len; i++)
        g_ptr_array_add(s->fwd, g_strdup(self->fwd_stack->pdata[i]));
    /* Title */
    char *base = g_path_get_basename(self->current_path ?: "/");
    g_strlcpy(s->title, base, sizeof(s->title));
    g_free(base);
}

void tab_session_restore(AetherWindow *self, int idx) {
    if (!self->tabs || idx < 0 || (guint)idx >= self->tabs->len) return;
    AetherTabSession *s = &g_array_index(self->tabs, AetherTabSession, idx);
    g_ptr_array_set_size(self->back_stack, 0);
    g_ptr_array_set_size(self->fwd_stack,  0);
    if (s->back) for (guint i = 0; i < s->back->len; i++)
        g_ptr_array_add(self->back_stack, g_strdup(s->back->pdata[i]));
    if (s->fwd) for (guint i = 0; i < s->fwd->len; i++)
        g_ptr_array_add(self->fwd_stack, g_strdup(s->fwd->pdata[i]));
    if (s->path)
        load_directory(self, s->path);
}

static int current_tab_index(AetherWindow *self) {
    if (!self->tab_view) return 0;
    return gtk_notebook_get_current_page(GTK_NOTEBOOK(self->tab_view));
}

void on_tab_selected(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer ud) {
    (void)notebook; (void)page;
    AetherWindow *self = AETHER_WINDOW(ud);
    tab_session_restore(self, page_num);
    /* Update tab title */
    if (self->current_path) {
        char *base = g_path_get_basename(self->current_path);
        GtkWidget *child = gtk_notebook_get_nth_page(notebook, page_num);
        if (child) {
            gtk_notebook_set_tab_label_text(notebook, child, base);
        }
        g_free(base);
    }
}

void new_tab(AetherWindow *self, const char *path) {
    if (!self->tabs) return;
    AetherTabSession session = { 0 };
    session.path = g_strdup(path ?: g_get_home_dir());
    session.back = g_ptr_array_new_with_free_func(g_free);
    session.fwd  = g_ptr_array_new_with_free_func(g_free);
    char *base = g_path_get_basename(session.path);
    g_strlcpy(session.title, base, sizeof(session.title));
    g_free(base);
    g_array_append_val(self->tabs, session);

    /* Add tab page to GtkNotebook */
    GtkWidget *placeholder = gtk_label_new("");
    int idx = gtk_notebook_append_page(GTK_NOTEBOOK(self->tab_view), placeholder, gtk_label_new(session.title));
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(self->tab_view), placeholder, TRUE);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(self->tab_view), idx);
}

void close_tab(AetherWindow *self, int idx) {
    if (!self->tabs || self->tabs->len <= 1) return;
    AetherTabSession *s = &g_array_index(self->tabs, AetherTabSession, idx);
    tab_session_free_fields(s);
    g_array_remove_index(self->tabs, idx);

    gtk_notebook_remove_page(GTK_NOTEBOOK(self->tab_view), idx);

    int new_idx = CLAMP(idx - 1, 0, (int)self->tabs->len - 1);
    tab_session_restore(self, new_idx);
}

gboolean on_new_tab_shortcut(GtkWidget *w, GVariant *a, gpointer ud) {
    (void)w; (void)a;
    AetherWindow *self = AETHER_WINDOW(ud);
    int old_idx = current_tab_index(self);
    tab_session_save(self, old_idx);
    new_tab(self, self->current_path);
    return TRUE;
}

gboolean on_close_tab_shortcut(GtkWidget *w, GVariant *a, gpointer ud) {
    (void)w; (void)a;
    AetherWindow *self = AETHER_WINDOW(ud);
    close_tab(self, current_tab_index(self));
    return TRUE;
}

