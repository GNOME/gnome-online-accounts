goabackend_inc = include_directories('.')
libgoa_backend_headers_built = []

libgoa_backend_sources = files(
  'goaoauth2provider.c',
  'goabackendinit.c',
  'goadlnaservermanager.c',
  'goaewsclient.c',
  'goaexchangeprovider.c',
  'goagoogleprovider.c',
  'goahttpclient.c',
  'goaimapauthlogin.c',
  'goaimapsmtpprovider.c',
  'goalastfmprovider.c',
  'goamailauth.c',
  'goamailclient.c',
  'goamediaserverprovider.c',
  'goaobjectskeletonutils.c',
  'goaowncloudprovider.c',
  'goaprovider.c',
  'goarestproxy.c',
  'goasmtpauth.c',
  'goasouplogger.c',
  'goautils.c',
  'goawebview.c',
  'goawindowsliveprovider.c',
  'nautilus-floating-bar.c'
)

enum_headers = files('goabackendenums.h')

headers = enum_headers + files(
  'goabackend.h',
  'goaprovider.h'
)

install_headers(
  headers,
  install_dir: join_paths(goa_pkgincludedir, 'goabackend')
)

enum = 'goabackendenumtypes'

enum_sources = gnome.mkenums(
  enum,
  sources: enum_headers,
  c_template: enum + '.c.template',
  h_template: enum + '.h.template',
  install_header: true,
  install_dir: join_paths(goa_pkgincludedir, 'goabackend')
)

libgoa_backend_headers_built += enum_sources[1]
libgoa_backend_sources += enum_sources

dbus = 'goadleynaservermanager'

dbus_sources = gnome.gdbus_codegen(
  dbus,
  dbus + '.xml',
  interface_prefix: 'com.intel.dLeynaServer.',
  namespace: 'DleynaServer',
  autocleanup: 'all',
)

libgoa_backend_headers_built += dbus_sources[1]
libgoa_backend_sources += dbus_sources

dbus = 'goadleynaservermediadevice'

dbus_sources = gnome.gdbus_codegen(
  dbus,
  dbus + '.xml',
  interface_prefix: 'com.intel.dLeynaServer.',
  namespace: 'DleynaServer',
  autocleanup: 'all',
)

libgoa_backend_headers_built += dbus_sources[1]
libgoa_backend_sources += dbus_sources

incs = common_incs

deps = [
  gtk_dep,
  javascript_core_gtk_dep,
  json_glib_dep,
  libgoa_dep,
  libsecret_dep,
  libsoup_dep,
  libxml_dep,
  rest_dep,
  webkit_gtk_dep
]

cflags = [
  '-DG_LOG_DOMAIN="GoaBackend"',
  '-DGOA_API_IS_SUBJECT_TO_CHANGE',
  '-DGOA_BACKEND_COMPILATION',
  '-DPACKAGE_LOCALE_DIR="@0@"'.format(goa_localedir),
  '-DPACKAGE_WEB_EXTENSIONS_DIR="@0@"'.format(join_paths(goa_pkglibdir, 'web-extensions'))
]

if enable_fedora
  libgoa_backend_sources += files('goafedoraprovider.c')
endif

if enable_kerberos
  libgoa_backend_sources += files('goakerberosprovider.c')
endif

if enable_fedora or enable_kerberos
  libgoa_backend_sources += identity_dbus_sources + identity_manager_error_src
  incs += goaidentity_inc
  deps += [gcr_dep]
endif

libgoa_backend = shared_library(
  goa_backend_api_name,
  libgoa_backend_sources,
  version: goa_backend_libversion,
  include_directories: incs,
  dependencies: deps,
  c_args: cflags,
  install: true
)

libgoa_backend_dep = declare_dependency(
  sources: libgoa_backend_headers_built,
  include_directories: goabackend_inc,
  dependencies: [gtk_dep, libgoa_dep],
  link_with: libgoa_backend
)

pkg.generate(
  libraries: libgoa_backend,
  version: goa_version,
  name: 'Goa Backends',
  description: 'Backends for GNOME Online Accounts Library',
  filebase: goa_backend_api_name,
  subdirs: goa_api_name,
  requires: [goa_api_name, 'gtk+-3.0'],
  variables: 'exec_prefix=${prefix}'
)

libgoawebextension_sources = files(
  'goawebextension.c',
  'goawebextensionmain.c'
)

deps = [
  rest_dep,
  webkit_gtk_dep
]

libgoa_web_extension = shared_module(
  'goawebextension',
  libgoawebextension_sources,
  include_directories: common_incs + [goa_inc],
  dependencies: deps,
  c_args: cflags,
  link_with: libgoa_backend,
  install: true,
  install_dir: join_paths(goa_pkglibdir, 'web-extensions')
)
