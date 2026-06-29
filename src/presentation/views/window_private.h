#ifndef AETHER_WINDOW_PRIVATE_H
#define AETHER_WINDOW_PRIVATE_H

#include "window.h"
#include "../../data/gio_file_repository.h"
#include "../../data/drive_manager.h"
#include "../../data/app_repository.h"
#include "../../data/privileged_file_manager.h"
#include "../controllers/clipboard_controller.h"
#include <gio/gio.h>
#include <gtk/gtk.h>

#define HISTORY_MAX 50

struct _AetherWindow {
    GtkApplicationWindow    parent_instance;
    AetherFileRepository   *repo;
    AetherDriveManager     *drive_mgr;
    AetherAppRepository    *app_repo;
    AetherClipboardController *clipboard;

    char    *current_path;
    gboolean show_hidden;

    GPtrArray *back_stack;
    GPtrArray *fwd_stack;

    guint item_count;
    guint selected_count;

    GtkWidget *grid_view;
    GtkWidget *list_view;
    GtkWidget *apps_view;
    GtkWidget *view_stack;

    GtkWidget *path_bar;
    GtkWidget *sidebar_list;
    GtkWidget *split_view;
    GtkWidget *search_bar;
    GtkWidget *search_entry;
    GtkWidget *status_label;
    GtkWidget *space_label;
    GtkWidget *btn_back;
    GtkWidget *btn_fwd;
    GtkWidget *btn_grid;
    GtkWidget *btn_list;
    GtkWidget *btn_toggle_sidebar;

    GtkFilterListModel *filter_model;
    GtkCustomFilter    *name_filter;
    GtkCustomSorter    *sorter;
    GtkMultiSelection  *grid_sel;
    GtkMultiSelection  *list_sel;
    char               *filter_string;
    int                 search_filter_type; // 0=All, 1=Media, 2=Doc, 3=Folder, 4=Apps, 5=Archive

    gboolean rubberband_active;
    double   rubberband_x0, rubberband_y0;

    int     sort_mode;
    gboolean sort_asc;
    GtkWidget *sort_btn;
    GtkWidget *progress_spinner;
    GtkWidget *btn_hidden;
    
    int icon_size;

    GFileMonitor *dir_monitor;

    int bookmark_row_start;

    GtkWidget  *tab_view;
    GArray     *tabs;

    GPtrArray  *undo_stack;
    GPtrArray  *redo_stack;

    GtkRecentManager *recent_mgr;

    /* وضع الصلاحيات المرتفعة - TRUE إذا كان المسار الحالي يتطلب صلاحيات root */
    gboolean elevated_mode;

    /*
     * load_serial: عداد يُزاد عند كل تحميل مجلد جديد.
     * يُستخدم في set_model_idle للتحقق أن النتيجة لا تزال حديثة.
     * يمنع race condition بين file monitor وexplicit load.
     */
    guint load_serial;
};


typedef struct {
    char      *path;
    GPtrArray *back;
    GPtrArray *fwd;
    char       title[64];
} AetherTabSession;

typedef enum { UNDO_TRASH, UNDO_RENAME, UNDO_MOVE } UndoOp;
typedef struct {
    UndoOp op;
    char  *src;
    char  *dest;
} UndoEntry;

/* Internal prototypes exposed to components */
void load_bookmarks(AetherWindow *self);
void save_bookmark(const char *path);
void remove_bookmark(const char *path);
gboolean is_bookmarked(const char *path);
void add_sidebar_separator(AetherWindow *self);
void add_sidebar_header(AetherWindow *self, const char *title);
void add_sidebar_place(AetherWindow *self, const char *name, const char *icon, const char *path);
void setup_sidebar(AetherWindow *self);
void on_sidebar_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data);
void setup_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d);
void bind_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d);
void unbind_grid_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d);
void setup_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d);
void bind_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d);
void unbind_list_item(GtkSignalListItemFactory *f, GtkListItem *li, gpointer d);
void setup_size_col_item(GtkListItemFactory *f, GtkListItem *item, gpointer ud);
void bind_size_col_item(GtkListItemFactory *f, GtkListItem *item, gpointer ud);
void unbind_size_col_item(GtkListItemFactory *f, GtkListItem *item, gpointer ud);
gint compare_entities(gconstpointer a, gconstpointer b, gpointer user_data);
gboolean name_filter_func(gpointer item, gpointer user_data);
void on_item_right_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
void on_background_right_clicked(GtkGestureClick *gesture, int n_press, double x, double y, gpointer user_data);
void on_popover_closed(GtkPopover *popover, gpointer user_data);
void load_directory(AetherWindow *self, const char *path);
void update_nav_buttons(AetherWindow *self);
void update_statusbar(AetherWindow *self);
void update_pathbar(AetherWindow *self);
void on_directory_loaded(GObject *source, GAsyncResult *res, gpointer user_data);
void setup_file_monitor(AetherWindow *self, const char *path);
void navigate_back(AetherWindow *self);
void navigate_forward(AetherWindow *self);
void on_dir_changed(GFileMonitor *mon, GFile *file, GFile *other, GFileMonitorEvent event, gpointer user_data);
void on_path_button_clicked(GtkButton *btn, gpointer user_data);
void on_item_activated(GtkWidget *view, guint position, gpointer user_data);
void on_new_folder_clicked(GtkButton *btn, gpointer user_data);
void on_new_folder_response(GtkDialog *d, int response_id, gpointer ud);
void on_new_document_clicked(GtkButton *btn, gpointer user_data);
void on_open_terminal_clicked(GtkButton *btn, gpointer user_data);
void on_select_all_clicked(GtkButton *btn, gpointer user_data);
void on_paste_toolbar_clicked(GtkButton *btn, gpointer user_data);
void on_paste_done(GObject *src, GAsyncResult *res, gpointer ud);
void aether_window_handle_dnd_move(AetherWindow *win, const char *dest_path, GPtrArray *src_paths);
void on_sort_mode_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data);
void on_sort_dir_clicked(GtkButton *btn, gpointer user_data);
void on_toggle_hidden(GSimpleAction *action, GVariant *param, gpointer user_data);
gboolean on_key_back(GtkWidget *w, GVariant *args, gpointer ud);
gboolean on_key_fwd(GtkWidget *w, GVariant *args, gpointer ud);
gboolean on_key_up(GtkWidget *w, GVariant *args, gpointer ud);
gboolean on_key_refresh(GtkWidget *w, GVariant *args, gpointer ud);
void on_add_bookmark_action(GSimpleAction *a, GVariant *p, gpointer ud);
void on_add_bookmark_path_action(GSimpleAction *a, GVariant *p, gpointer ud);
void on_remove_bookmark_action(GSimpleAction *a, GVariant *p, gpointer ud);
void on_remove_bookmark_path_action(GSimpleAction *a, GVariant *p, gpointer ud);
gboolean on_drop_target(GtkDropTarget *t, const GValue *val, double x, double y, gpointer ud);
void setup_drag_drop(AetherWindow *self, GtkWidget *view);
GdkContentProvider *on_drag_prepare(GtkDragSource *source, double x, double y, gpointer user_data);
gboolean on_item_drop_accept(GtkDropTarget *target, GdkDrop *drop, gpointer user_data);
gboolean on_item_drop(GtkDropTarget *target, const GValue *value, double x, double y, gpointer user_data);
gboolean on_undo_shortcut(GtkWidget *w, GVariant *a, gpointer ud);
gboolean on_ctrl_h_shortcut(GtkWidget *w, GVariant *a, gpointer ud);
gboolean on_ctrl_d_shortcut(GtkWidget *w, GVariant *a, gpointer ud);
void push_undo(AetherWindow *self, UndoOp op, const char *src, const char *dest);
void do_undo(AetherWindow *self);
void on_btn_back_clicked(GtkButton *btn, gpointer user_data);
void on_btn_fwd_clicked(GtkButton *btn, gpointer user_data);
void on_btn_toggle_sidebar_toggled(GtkToggleButton *btn, gpointer user_data);
void on_view_switched(GtkButton *btn, gpointer user_data);
void on_search_changed(GtkSearchEntry *entry, gpointer user_data);
void on_search_filter_changed(GtkDropDown *dropdown, GParamSpec *pspec, gpointer user_data);
void on_search_mode_toggled(GObject *object, GParamSpec *pspec, gpointer user_data);
void tab_session_free_fields(AetherTabSession *s);
void tab_session_save(AetherWindow *self, int idx);
void tab_session_restore(AetherWindow *self, int idx);
void on_tab_selected(GtkNotebook *notebook, GtkWidget *page, guint page_num, gpointer ud);
void new_tab(AetherWindow *self, const char *path);
void close_tab(AetherWindow *self, int idx);
gboolean on_new_tab_shortcut(GtkWidget *w, GVariant *a, gpointer ud);
gboolean on_close_tab_shortcut(GtkWidget *w, GVariant *a, gpointer ud);

/* window_apps.h included here to avoid circular dependencies if possible, or we just declare functions */
GtkWidget *setup_apps_view(AetherWindow *self);
void show_apps_view(AetherWindow *self);

/* ── دوال الصلاحيات المرتفعة ── */
void on_elevated_list_done(GObject *source, GAsyncResult *res, gpointer ud);
void load_directory_elevated(AetherWindow *self, const char *path);
void show_elevation_error(AetherWindow *self, const char *path, const char *message);

#endif // AETHER_WINDOW_PRIVATE_H
