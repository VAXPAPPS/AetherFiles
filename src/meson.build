aetherfiles_sources = [
  'main.c',
  'domain/file_entity.c',
  'domain/file_repository.c',
  'data/gio_file_repository.c',
  'presentation/application.c',
  'presentation/views/window.c',
  'presentation/controllers/clipboard_controller.c',
]

executable('aetherfiles',
  aetherfiles_sources,
  aetherfiles_resources,
  dependencies: [gtk_dep, gio_dep, gio_unix_dep],
  include_directories: inc,
  install: true
)
