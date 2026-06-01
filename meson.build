project(
  'hostnamed',
  'c',
  version         : '0.1.0',
  license         : 'GPL-2.0-or-later',
  meson_version   : '>= 0.58.0',
  default_options : [
    'c_std=gnu11',
    'warning_level=2',
    'werror=false',
  ],
)

# ─── Compiler flags ───────────────────────────────────────────────────────────

cc = meson.get_compiler('c')

# Flags accepted by GCC/Clang that improve safety and catch common bugs.
extra_c_args = cc.get_supported_arguments([
  '-Wno-unused-parameter',      # GLib callbacks often leave params unused
  '-Wno-missing-field-initializers',
  '-fstack-protector-strong',
  '-D_FORTIFY_SOURCE=2',
])

add_project_arguments(extra_c_args, language : 'c')

# ─── Build-time options ───────────────────────────────────────────────────────

# See meson_options.txt for the full set.  Defaults are set there; here we
# just read them.
prefix      = get_option('prefix')
bindir      = get_option('bindir')       # relative to prefix
sbindir     = get_option('sbindir')      # relative to prefix
sysconfdir  = get_option('sysconfdir')   # /etc  (absolute or relative)
datadir     = get_option('datadir')
localedir   = join_paths(prefix, get_option('localedir'))

# Derive absolute paths for paths that may be given as relative
if not sysconfdir.startswith('/')
  sysconfdir = join_paths(prefix, sysconfdir)
endif

# ─── Required dependencies ────────────────────────────────────────────────────

glib_req     = '>= 2.56'
polkit_req   = '>= 0.113'

glib_dep = dependency('glib-2.0',     version : glib_req)
gio_dep  = dependency('gio-2.0',      version : glib_req)
gio_unix = dependency('gio-unix-2.0', version : glib_req)
polkit   = dependency('polkit-gobject-1', version : polkit_req,
             required : get_option('polkit'))

# If polkit was found (or required), expose a compile-time flag so the source
# can #ifdef around the polkit calls if ever needed for testing without polkit.
polkit_found = polkit.found()
if polkit_found
  add_project_arguments('-DHAVE_POLKIT=1', language : 'c')
endif

# ─── i18n / gettext ───────────────────────────────────────────────────────────

i18n = import('i18n')

gettext_package = meson.project_name()
add_project_arguments(
  '-DGETTEXT_PACKAGE="@0@"'.format(gettext_package),
  language : 'c',
)

# ─── config.h ─────────────────────────────────────────────────────────────────

conf = configuration_data()
conf.set_quoted('PACKAGE',         meson.project_name())
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('VERSION',         meson.project_version())
conf.set_quoted('GETTEXT_PACKAGE', gettext_package)
conf.set_quoted('LOCALEDIR',       localedir)
conf.set_quoted('SYSCONFDIR',      sysconfdir)
conf.set_quoted('DATADIR',         join_paths(prefix, datadir))
conf.set_quoted('SBINDIR',         join_paths(prefix, sbindir))

# Distro-specific tunables from meson_options.txt.
# PRIVILEGED_GROUP is used by polkit rule evaluation to decide which local
# users are considered administrators without a password prompt.
conf.set_quoted('PRIVILEGED_GROUP', get_option('privileged-group'))
# HOSTNAME_FILE is the path the daemon reads/writes for the static hostname.
# /etc/HOSTNAME on Slackware, /etc/hostname on most other distros.
conf.set_quoted('HOSTNAME_FILE',    get_option('HOSTNAME'))

# GLIB_VERSION_* guards – target the minimum we require so we get deprecation
# warnings for anything we use that was removed after glib_req.
conf.set('GLIB_VERSION_MIN_REQUIRED', 'GLIB_VERSION_2_56')
conf.set('GLIB_VERSION_MAX_ALLOWED',  'GLIB_VERSION_2_56')

configure_file(
  output        : 'config.h',
  configuration : conf,
)

# Make config.h visible to all subdirs without needing an explicit include path.
config_h_inc = include_directories('.')

# ─── Subdirectories ───────────────────────────────────────────────────────────

subdir('src')
subdir('data')
subdir('po')

# ─── Summary ──────────────────────────────────────────────────────────────────

summary({
  'prefix'       : prefix,
  'sbindir'      : join_paths(prefix, sbindir),
  'sysconfdir'   : sysconfdir,
  'datadir'      : join_paths(prefix, datadir),
  'localedir'    : localedir,
}, section : 'Directories')

summary({
  'polkit' : polkit_found,
  'GLib'   : glib_dep.version(),
}, section : 'Dependencies')
