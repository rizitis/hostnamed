project(
  'hostnamed',
  'c',
  version         : '0.2.0',
  license         : 'GPL-2.0-or-later',
  meson_version   : '>= 0.58.0',
  default_options : [
    'c_std=gnu11',
    'warning_level=2',
    'werror=false',
  ],
)

cc = meson.get_compiler('c')

extra_c_args = cc.get_supported_arguments([
  '-Wno-unused-parameter',      # GLib callbacks often leave params unused
  '-Wno-missing-field-initializers',
  '-fstack-protector-strong',
  '-D_FORTIFY_SOURCE=2',
])

add_project_arguments(extra_c_args, language : 'c')

prefix     = get_option('prefix')
sbindir    = get_option('sbindir')
sysconfdir = get_option('sysconfdir')
datadir    = get_option('datadir')
localedir  = join_paths(prefix, get_option('localedir'))

if not sysconfdir.startswith('/')
  sysconfdir = join_paths(prefix, sysconfdir)
endif

abs_datadir = join_paths(prefix, datadir)
abs_sbindir = join_paths(prefix, sbindir)

glib_req   = '>= 2.56'
polkit_req = '>= 0.113'

glib_dep = dependency('glib-2.0',         version : glib_req)
gio_dep  = dependency('gio-2.0',          version : glib_req)
gio_unix = dependency('gio-unix-2.0',     version : glib_req)
polkit   = dependency('polkit-gobject-1', version : polkit_req,
             required : get_option('polkit'))

polkit_found = polkit.found()
if polkit_found
  add_project_arguments('-DHAVE_POLKIT=1', language : 'c')
endif

i18n = import('i18n')

gettext_package = meson.project_name()
add_project_arguments(
  '-DGETTEXT_PACKAGE="@0@"'.format(gettext_package),
  language : 'c',
)

conf = configuration_data()
conf.set_quoted('PACKAGE',         meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('VERSION',         meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', gettext_package)
conf.set_quoted('LOCALEDIR',       localedir)
conf.set_quoted('SYSCONFDIR',      sysconfdir)
conf.set_quoted('DATADIR',         abs_datadir)
conf.set_quoted('SBINDIR',         abs_sbindir)

# Distro-specific tunables (meson_options.txt).
# PRIVILEGED_GROUP: group considered "admin" by polkit rules.
# HOSTNAME_FILE: path to the static hostname file
#   (/etc/HOSTNAME on Slackware, /etc/hostname elsewhere).
conf.set_quoted('PRIVILEGED_GROUP', get_option('privileged-group'))
conf.set_quoted('HOSTNAME_FILE',    get_option('HOSTNAME'))

conf.set('GLIB_VERSION_MIN_REQUIRED', 'GLIB_VERSION_2_56')
conf.set('GLIB_VERSION_MAX_ALLOWED',  'GLIB_VERSION_2_56')

configure_file(
  output        : 'config.h',
  configuration : conf,
)

config_h_inc = include_directories('.')

subdir('src')
subdir('data')
subdir('po')

summary({
  'prefix'     : prefix,
  'sbindir'    : abs_sbindir,
  'sysconfdir' : sysconfdir,
  'datadir'    : abs_datadir,
  'localedir'  : localedir,
}, section : 'Directories')

summary({
  'polkit' : polkit_found,
  'GLib'   : glib_dep.version(),
}, section : 'Dependencies')
