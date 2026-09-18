project(
    'wf-panel-pi',
    'c',
    version: run_command(['dpkg-parsechangelog', '--show-field', 'Version']).stdout().strip(),
    meson_version: '>=0.60.0',
    default_options: [
        'c_std=c11',
        'warning_level=2',
        'werror=false',
    ],
)

add_project_arguments(['-Wno-pedantic', '-Wno-unused-parameter', '-Wno-parentheses'], language: 'c')
add_project_arguments('-D_GNU_SOURCE', language : [ 'c' ])

share_dir = join_paths(get_option('prefix'), 'share')
applications_dir = join_paths(share_dir, 'applications')
resource_dir = join_paths(share_dir, 'wf-panel-pi')
presource_dir = join_paths(share_dir, 'rpcc')
ui_dir = join_paths(resource_dir, 'ui')
pui_dir = join_paths(presource_dir, 'ui')
sysconf_dir = join_paths(get_option('prefix'), get_option('sysconfdir'))

gettext_package = meson.project_name()

add_project_arguments('-DPACKAGE_DATA_DIR="' + resource_dir + '"', language : [ 'c' ])
add_project_arguments('-DGETTEXT_PACKAGE="' + gettext_package + '"', language : [ 'c' ])
add_project_arguments('-DPACKAGE_LOCALE_DIR="' + share_dir + '/locale"', language : [ 'c' ])

wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols')

subdir('proto')

gtk = dependency('gtk+-3.0', version: '>=3.24')
deps = [ wf_protos, gtk ]

subdir('data')
subdir('src')
subdir('po')

pkgconfig = import('pkgconfig')
pkgconfig.generate(
    filebase:     meson.project_name(),
    name:         meson.project_name(),
    description: 'Panel for Raspberry Pi Wayland desktop',
    subdirs:     'wf-panel-pi'
)
