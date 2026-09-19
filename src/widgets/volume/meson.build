name = 'volume'

pipewire = dependency('libpipewire-0.3')
menu_cache = dependency('libmenu-cache')

sources = files(
  'volume.c',
  'commongui.c',
  'bluetooth.c',
  'pipewire.c',
  'volume_widget.c'
)

module_deps = [ pipewire, menu_cache ]

shared_module(name, sources,
        dependencies: deps + module_deps,
        install: true,
        install_dir: get_option('libdir') / 'wf-panel-pi',
        c_args : [ '-DPLUGIN_NAME="' + name + '"', '-DUSES_MENUCACHE' ],
        include_directories : incdir
)
