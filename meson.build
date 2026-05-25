project('AetherFiles', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2'
  ]
)

gnome = import('gnome')
cc = meson.get_compiler('c')

# Dependencies
gtk_dep = dependency('gtk4')

gio_dep = dependency('gio-2.0')
gio_unix_dep = dependency('gio-unix-2.0')

inc = include_directories('src')

# Subdirectories
subdir('data')
subdir('src')
