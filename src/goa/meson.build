libgoa_deps_public = [
  gio_dep,
  glib_dep,
]

libgoa_deps = libgoa_deps_public + [
  gio_unix_dep,
]

libgoa_headers_static = files(
  'goaclient.h',
  'goaerror.h',
  'goa.h',
  'goaversion.h'
)

libgoa_headers_built = []

libgoa_sources = files(
  'goaclient.c',
  'goaerror.c',
  'goaversion.c'
)

enum_headers = files(
  'goaenums.h',
)

libgoa_headers_static += enum_headers

goaconfig_conf = configuration_data()
goaconfig_conf.set('GOA_MAJOR_VERSION', goa_major_version)
goaconfig_conf.set('GOA_MINOR_VERSION', goa_minor_version)
goaconfig_conf.set('GOA_MICRO_VERSION', goa_micro_version)

goaconfig_h = configure_file(
  output: 'goaconfig.h',
  configuration: goaconfig_conf,
  install: true,
  install_dir: join_paths(goa_pkglibdir, 'include')
)

libgoa_headers_built += goaconfig_h
libgoa_sources += goaconfig_h

dbus = 'goa-generated'

dbus_sources = gnome.gdbus_codegen(
  dbus,
  sources: dbus_ifaces,
  interface_prefix: 'org.gnome.OnlineAccounts.',
  namespace: goa_ns,
  autocleanup: 'all',
  object_manager: true,
  docbook: dbus + '-doc',
  install_header: true,
  install_dir: join_paths(goa_pkgincludedir, goa_name)
)

libgoa_headers_built += dbus_sources[1]
libgoa_sources += dbus_sources

enum_sources = gnome.mkenums_simple(
  'goaenumtypes',
  sources: enum_headers,
  install_header: true,
  install_dir: join_paths(goa_pkgincludedir, goa_name)
)

libgoa_headers_built += enum_sources[1]
libgoa_sources += enum_sources

cflags = [
  '-DG_LOG_DOMAIN="@0@"'.format(goa_ns),
  '-DGOA_COMPILATION'
]

install_headers(
  libgoa_headers_static,
  install_dir: join_paths(goa_pkgincludedir, goa_name)
)

libgoa = shared_library(
  goa_api_name,
  sources: libgoa_sources,
  version: goa_libversion,
  include_directories: common_incs,
  dependencies: libgoa_deps,
  c_args: cflags,
  install: true
)

libgoa_dep = declare_dependency(
  sources: libgoa_headers_built,
  include_directories: src_inc,
  dependencies: libgoa_deps_public,
  link_with: libgoa
)

meson.override_dependency(goa_api_name, libgoa_dep)

pkg.generate(
  libraries: libgoa,
  version: goa_version,
  name: goa_ns,
  description: 'GNOME Online Accounts Library',
  filebase: goa_api_name,
  subdirs: goa_api_name,
  extra_cflags: ['-I${libdir}/@0@/include'.format(goa_api_name)],
  requires: libgoa_deps_public,
  variables: 'exec_prefix=${prefix}'
)

if enable_introspection
  libgoa_gir = gnome.generate_gir(
    libgoa,
    sources: libgoa_sources + libgoa_headers_static,
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
