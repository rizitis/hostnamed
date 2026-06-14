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
  dependencies          : hostnamed_deps,
  include_directories   : hostnamed_inc,
  install               : true,
  install_dir           : get_option('libexecdir'),
  gnu_symbol_visibility : 'hidden',
  c_args                : [
    '-DGLIB_DISABLE_DEPRECATION_WARNINGS',
  ],
)
