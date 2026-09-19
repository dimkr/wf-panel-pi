name = 'batt'

sources = files(
  'batt.c',
  'batt_sys.c',
  'batt_widget.c'
)

shared_module(name, sources,
        dependencies: deps,
        install: true,
        install_dir: get_option('libdir') / 'wf-panel-pi',
        c_args : '-DPLUGIN_NAME="' + name + '"',
        include_directories : incdir
)
