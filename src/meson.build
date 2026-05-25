aetherfiles_sources = [
  'main.c',
  'domain/file_entity.c',
  'domain/drive_entity.c',
  'domain/app_entity.c',
  'domain/file_repository.c',
  'data/gio_file_repository.c',
  'data/drive_manager.c',
  'data/app_repository.c',
  'presentation/application.c',
  'presentation/views/window.c',
  'presentation/views/window_sidebar.c',
  'presentation/views/window_views.c',
  'presentation/views/window_nav.c',
  'presentation/views/window_actions.c',
  'presentation/views/window_tabs.c',
  'presentation/views/window_context_menu.c',
  'presentation/views/window_apps.c',
  'presentation/controllers/clipboard_controller.c',
]

executable('aetherfiles',
  aetherfiles_sources,
  aetherfiles_resources,
  dependencies: [gtk_dep, gio_dep, gio_unix_dep],
  include_directories: inc,
  install: true
)
