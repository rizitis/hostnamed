# ─────────────────────────────────────────────────────────────────────────────
# src/meson.build
# ─────────────────────────────────────────────────────────────────────────────

# Sources live next to this file; the generated config.h is in the build root,
# made available by config_h_inc defined in the top-level meson.build.
hostnamed_sources = files(
  'main.c',
  'rcl-hostname.c',
)

hostnamed_deps = [
  glib_dep,
  gio_dep,
  gio_unix,
]

if polkit_found
  hostnamed_deps += polkit
endif

hostnamed_inc = [
  config_h_inc,
  include_directories('.'),
]

executable(
  'hostnamed',
  hostnamed_sources,
  dependencies        : hostnamed_deps,
  include_directories : hostnamed_inc,
  install             : true,
  install_dir         : get_option('libexecdir'),
  # Export as few symbols as possible – this is a daemon, not a library.
  gnu_symbol_visibility : 'hidden',
  c_args              : [
    # Silence GLib internal deprecation warnings that come from the GObject
    # boilerplate macros on older GLib versions.
    '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
  ],
)
