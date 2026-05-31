# ─────────────────────────────────────────────────────────────────────────────
# data/meson.build
# ─────────────────────────────────────────────────────────────────────────────

# ─── Resolve install directories ──────────────────────────────────────────────
#
# Each directory can be overridden via -D<option>=... at configure time.
# If left empty we query dbus-1 / polkit pkg-config and fall back to sensible
# defaults.  There is no systemd dependency – the daemon is activated purely
# by dbus-daemon via the .service file.

# --- D-Bus system service directory ------------------------------------------
_dbus_srv_opt = get_option('dbus_system_service_dir')
if _dbus_srv_opt != ''
  dbus_service_dir = _dbus_srv_opt
else
  dbus_dep = dependency('dbus-1', required : false)
  if dbus_dep.found()
    dbus_service_dir = dbus_dep.get_variable(
      pkgconfig        : 'system_bus_services_dir',
      pkgconfig_define : ['datadir', join_paths(prefix, datadir)],
    )
  else
    dbus_service_dir = join_paths(prefix, datadir, 'dbus-1', 'system-services')
  endif
endif

# --- D-Bus system policy directory -------------------------------------------
_dbus_conf_opt = get_option('dbus_system_conf_dir')
if _dbus_conf_opt != ''
  dbus_conf_dir = _dbus_conf_opt
else
  dbus_dep = dependency('dbus-1', required : false)
  if dbus_dep.found()
    # dbus-1.pc exposes sysconfdir; the policy files live under
    # <sysconfdir>/dbus-1/system.d on every distro we care about.
    _dbus_sysconfdir = dbus_dep.get_variable(
      pkgconfig        : 'sysconfdir',
      pkgconfig_define : ['sysconfdir', sysconfdir],
    )
    dbus_conf_dir = join_paths(_dbus_sysconfdir, 'dbus-1', 'system.d')
  else
    dbus_conf_dir = join_paths(sysconfdir, 'dbus-1', 'system.d')
  endif
endif

# --- polkit policy directory -------------------------------------------------
_polkit_opt = get_option('polkit_policy_dir')
if _polkit_opt != ''
  polkit_policy_dir = _polkit_opt
else
  if polkit_found
    polkit_policy_dir = polkit.get_variable(
      pkgconfig        : 'actiondir',
      pkgconfig_define : ['datadir', join_paths(prefix, datadir)],
    )
  else
    polkit_policy_dir = join_paths(prefix, datadir, 'polkit-1', 'actions')
  endif
endif

# ─── D-Bus service file ───────────────────────────────────────────────────────
#
# Tells dbus-daemon how to launch hostnamed on demand (D-Bus activation).
# The daemon runs as root; dbus-daemon spawns it when the first client calls
# a method on org.freedesktop.hostname1.

dbus_service_conf = configuration_data()
dbus_service_conf.set('SBINDIR', join_paths(prefix, get_option('sbindir')))

configure_file(
  input         : 'org.freedesktop.hostname1.service.in',
  output        : 'org.freedesktop.hostname1.service',
  configuration : dbus_service_conf,
  install       : true,
  install_dir   : dbus_service_dir,
)

# ─── D-Bus system policy (bus security) ──────────────────────────────────────
#
# Grants ownership of the well-known name to root and allows any user to send
# method calls.  The Set* methods are further gated by polkit inside the daemon.

install_data(
  'org.freedesktop.hostname1.conf',
  install_dir : dbus_conf_dir,
)

# ─── polkit policy ────────────────────────────────────────────────────────────

if polkit_found
  install_data(
    'org.freedesktop.hostname1.policy',
    install_dir : polkit_policy_dir,
  )
endif

# ─── Summary ──────────────────────────────────────────────────────────────────

summary({
  'D-Bus service dir' : dbus_service_dir,
  'D-Bus conf dir'    : dbus_conf_dir,
  'polkit policy dir' : polkit_policy_dir,
}, section : 'Data files')
