libgoa_backend_headers_built = []

libgoa_backend_sources = files(
  'goaauthflowbutton.c',
  'goaoauth2provider.c',
  'goabackendinit.c',
  'goadavclient.c',
  'goadavconfig.c',
  'goaewsclient.c',
  'goaexchangeprovider.c',
  'goagoogleprovider.c',
  'goaimapauthlogin.c',
  'goaimapsmtpprovider.c',
  'goamailauth.c',
  'goamailclient.c',
  'goamailconfig.c',
  'goamsgraphprovider.c',
  'goaobjectskeletonutils.c',
  'goaowncloudprovider.c',
  'goaprovider.c',
  'goaproviderdialog.c',
  'goarestproxy.c',
  'goaserviceconfig.c',
  'goasmtpauth.c',
  'goasouplogger.c',
  'goautils.c',
  'goawebdavprovider.c',
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

enum_sources = gnome.mkenums_simple(
  'goabackendenumtypes',
  body_prefix: '#include <glib-object.h>',
  sources: enum_headers,
  install_header: true,
  install_dir: join_paths(goa_pkgincludedir, 'goabackend')
)

libgoa_backend_headers_built += enum_sources[1]
libgoa_backend_sources += enum_sources

priv_enum_sources = gnome.mkenums_simple(
  'goabackendenumtypes-priv',
  body_prefix: '#include <glib-object.h>',
  sources: [
    'goabackendenums-priv.h',
    'goaproviderdialog.h',
  ],
  install_header: false,
)

libgoa_backend_headers_built += priv_enum_sources[1]
libgoa_backend_sources += priv_enum_sources

incs = common_incs

deps = [
  gtk_dep,
  json_glib_dep,
  libadwaita_dep,
  libgoa_dep,
  libsecret_dep,
  libsoup_dep,
  libxml_dep,
  rest_dep,
]

cflags = [
  '-DG_LOG_DOMAIN="GoaBackend"',
  '-DGOA_API_IS_SUBJECT_TO_CHANGE',
  '-DGOA_BACKEND_COMPILATION',
  '-DGOA_CONF_FILENAME="@0@"'.format(join_paths(get_option('sysconfdir'), 'goa.conf')),
  '-DPACKAGE_LOCALE_DIR="@0@"'.format(goa_localedir),
]

if enable_fedora
  libgoa_backend_sources += files('goakerberosprovider.c')
  libgoa_backend_sources += files('goafedoraprovider.c')
elif enable_kerberos
  libgoa_backend_sources += files('goakerberosprovider.c')
endif

if enable_fedora or enable_kerberos
  libgoa_backend_sources += identity_dbus_sources + identity_error_sources
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
  include_directories: src_inc,
  dependencies: [gtk_dep, libgoa_dep],
  link_with: libgoa_backend
)

meson.override_dependency(goa_backend_api_name, libgoa_backend_dep)

pkg.generate(
  libraries: libgoa_backend,
  version: goa_version,
  name: 'Goa Backends',
  description: 'Backends for GNOME Online Accounts Library',
  filebase: goa_backend_api_name,
  subdirs: goa_api_name,
  requires: [goa_api_name, 'gtk4', 'libadwaita-1'],
  variables: 'exec_prefix=${prefix}'
)

executable('goa-oauth2-handler',
  'goaoauth2handler.c',
  dependencies: [
    glib_dep,
    gio_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="@0@"'.format('goa-oauth2-handler'),
  ],
  include_directories: common_incs,
  install: true,
  install_dir: goa_libexecdir,
)
