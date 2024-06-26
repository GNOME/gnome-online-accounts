goaidentity_inc = include_directories('.')

headers = files(
  'goaalarm.h',
  'goaidentity.h',
  'goaidentityinquiry.h',
  'goaidentityinquiryprivate.h',
  'goaidentityservice.h',
  'goaidentitymanagerprivate.h',
  'goaidentitymanager.h',
  'goaidentitymanagererror.h',
  'goaidentityutils.h',
  'goakerberosidentity.h',
  'goakerberosidentityinquiry.h',
  'goakerberosidentitymanager.h',
  'goalinuxnotificationstream.h',
)

dbus = 'org.gnome.Identity'

identity_dbus_sources = gnome.gdbus_codegen(
  dbus,
  dbus + '.xml',
  interface_prefix: dbus + '.',
  namespace: 'GoaIdentityService',
  object_manager: true,
  annotations: [dbus, 'org.gtk.GDBus.C.Name', 'Identity']
)

identity_manager_error_src = files('goaidentitymanagererror.c')

sources = identity_manager_error_src + files(
  'goaalarm.c',
  'goaidentity.c',
  'goaidentityinquiry.c',
  'goaidentityservice.c',
  'goaidentitymanager.c',
  'goaidentityutils.c',
  'goakerberosidentity.c',
  'goakerberosidentityinquiry.c',
  'goakerberosidentitymanager.c',
  'goalinuxnotificationstream.c',
  'main.c'
)

enum = 'goaidentityenumtypes'

sources += gnome.mkenums(
  enum,
  sources: headers,
  c_template: enum + '.c.in',
  h_template: enum + '.h.in'
)

deps = [
  gcr_dep,
  gio_unix_dep,
  krb5_dep,
  libgoa_dep,
  libkeyutils_dep,
]

cflags = [
  '-DG_LOG_DOMAIN="libgoaidentity"',
  '-DGOA_COMPILATION',
  '-DGOA_API_IS_SUBJECT_TO_CHANGE',
  '-DGOA_BACKEND_API_IS_SUBJECT_TO_CHANGE',
  '-DPACKAGE_LOCALE_DIR="@0@"'.format(goa_localedir),
]

executable(
  'goa-identity-service',
  sources + [identity_dbus_sources],
  include_directories: common_incs,
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_dir: goa_libexecdir
)
