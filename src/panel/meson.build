c_sources = files(
  'plug_conf.c',
  'notification.c',
  'plugin.c',
  'panel.c',
  'panel-app.c',
  'autohide-window.c'
)

proj_args = [ '-DPLUGIN_PATH="' + get_option ('prefix') + '/' + get_option('libdir') + '/wf-panel-pi/"', '-DUSES_MENUCACHE' ]

gtklayershell = dependency('gtk-layer-shell-0', version: '>= 0.1', fallback: ['gtk-layer-shell', 'gtk_layer_shell_dep'])
udev = dependency('libudev')
input = dependency('libinput')
evdev = dependency('libevdev')
libxml2 = dependency('libxml-2.0')
gio = dependency('gio-2.0')
menu_cache = dependency('libmenu-cache')
panel_deps = [ gtklayershell, udev, input, evdev, libxml2, gio, menu_cache ]

executable('wf-panel-pi', c_sources,
        dependencies: deps + panel_deps,
        install: true,
        export_dynamic: true,
        c_args : proj_args,
        include_directories : incdir
)
