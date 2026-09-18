name = 'tray'

sources = files(
  'watcher.c',
  'tray.c',
  'item.c',
  'host.c'
)

dbusmenu_gtk = dependency('dbusmenu-gtk3-0.4')
module_deps = [ dbusmenu_gtk ]

shared_module(name, sources,
        dependencies: deps + module_deps,
        install: true,
        install_dir: get_option('libdir') / 'wf-panel-pi',
        c_args : '-DPLUGIN_NAME="' + name + '"',
        include_directories : incdir
)
