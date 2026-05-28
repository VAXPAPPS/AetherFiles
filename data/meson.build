gnome = import('gnome')

aetherfiles_resources = gnome.compile_resources(
  'aetherfiles-resources',
  'com.aetheros.files.gresource.xml',
  source_dir: meson.current_source_dir(),
  c_name: 'aetherfiles'
)

# ── تثبيت ملف سياسة Polkit ──
install_data(
  'com.aetheros.files.policy',
  install_dir: '/usr/share/polkit-1/actions'
)
