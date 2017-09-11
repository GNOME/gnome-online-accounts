goa_inc = include_directories('.')

enum_headers = files('goaenums.h')

headers = enum_headers + files(
  'goaclient.h',
  'goaerror.h',
  'goa.h',
  'goaversion.h'
)

install_headers(
  headers,
  install_dir: join_paths(goa_pkgincludedir, goa_name)
)

goaconfig_conf = configuration_data()
goaconfig_conf.set('GOA_MAJOR_VERSION', goa_major_version)
goaconfig_conf.set('GOA_MINOR_VERSION', goa_minor_version)
goaconfig_conf.set('GOA_MICRO_VERSION', goa_micro_version)

goaconfig = 'goaconfig.h'

goaconfig_h = configure_file(
  input: goaconfig + '.in',
  output: goaconfig,
  configuration: goaconfig_conf,
  install: true,
  install_dir: join_paths(goa_pkglibdir, 'include')
)

dbus = 'goa-generated'

dbus_sources = gnome.gdbus_codegen(
  dbus,
  sources: dbus_ifaces,
  interface_prefix: 'org.gnome.OnlineAccounts.',
  namespace: goa_ns,
  object_manager: true,
  docbook: dbus + '-doc',
  install_header: true,
  install_dir: join_paths(goa_pkgincludedir, goa_name)
)

enum = 'goaenumtypes'

enum_sources = gnome.mkenums(
  enum,
  sources: enum_headers,
  c_template: enum + '.c.template',
  h_template: enum + '.h.template',
  install_header: true,
  install_dir: join_paths(goa_pkgincludedir, goa_name)
)

sources = [goaconfig_h] + dbus_sources + enum_sources + files(
  'goaclient.c',
  'goaerror.c',
  'goaversion.c'
)

deps = [
  gio_dep,
  glib_dep
]

cflags = [
  '-DG_LOG_DOMAIN="@0@"'.format(goa_ns),
  '-DGOA_COMPILATION'
]

libgoa = shared_library(
  goa_api_name,
  sources: sources,
  version: goa_libversion,
  include_directories: common_incs,
  dependencies: deps + [gio_unix_dep],
  c_args: cflags,
  install: true
)

libgoa_dep = declare_dependency(
  sources: [goaconfig_h, dbus_sources[1], enum_sources[1]],
  include_directories: goa_inc,
  dependencies: deps,
  link_with: libgoa
)

pkg.generate(
  libraries: libgoa,
  version: goa_version,
  name: goa_ns,
  description: 'GNOME Online Accounts Library',
  filebase: goa_api_name,
  subdirs: goa_api_name,
  extra_cflags: ['-I${libdir}/@0@/include'.format(goa_api_name)],
  requires: deps,
  variables: 'exec_prefix=${prefix}'
)

if enable_introspection
  libgoa_gir = gnome.generate_gir(
    libgoa,
    sources: sources + headers,
    includes: 'Gio-2.0',
    nsversion: goa_api_version,
    namespace: goa_ns,
    export_packages: goa_api_name,
    extra_args: '-DGOA_COMPILATION',
    header: join_paths(goa_name, 'goa.h'),
    install: true
  )

  if enable_vapi
    gnome.generate_vapi(
      goa_api_name,
      sources: libgoa_gir[0],
      packages: 'gio-2.0',
      install: true
    )
  endif
endif
