project(
  'gnome-online-accounts', 'c',
  version: '3.31.90',
  license: 'LGPL2+',
  default_options: 'buildtype=debugoptimized',
  meson_version: '>= 0.47.2'
)

goa_name = 'goa'

goa_version = meson.project_version()
version_array = goa_version.split('.')
goa_major_version = version_array[0].to_int()
goa_minor_version = version_array[1].to_int()
goa_micro_version = version_array[2].to_int()

goa_ns = 'Goa'

goa_api_version = '1.0'
goa_api_name = '@0@-@1@'.format(goa_name, goa_api_version)
goa_backend_api_name = '@0@-backend-@1@'.format(goa_name, goa_api_version)

goa_gir_version = '1.0'

goa_prefix = get_option('prefix')
goa_datadir = join_paths(goa_prefix, get_option('datadir'))
goa_includedir = join_paths(goa_prefix, get_option('includedir'))
goa_libdir = join_paths(goa_prefix, get_option('libdir'))
goa_libexecdir = join_paths(goa_prefix, get_option('libexecdir'))
goa_localedir = join_paths(goa_prefix, get_option('localedir'))
goa_mandir = join_paths(goa_prefix, get_option('mandir'))

goa_pkgdatadir = join_paths(goa_datadir, meson.project_name())
goa_pkgincludedir = join_paths(goa_includedir, goa_api_name)
goa_pkglibdir = join_paths(goa_libdir, goa_api_name)

# Before making a release, the LT_VERSION string should be modified.
# The string is of the form C:R:A.
# - If interfaces have been changed or added, but binary compatibility has
#   been preserved, change to C+1:0:A+1
# - If binary compatibility has been broken (eg removed or changed interfaces)
#   change to C+1:0:0
# - If the interface is the same as the previous version, change to C:R+1:A
#
# libversion = '@0@.@1@.@2@'.format(soversion, current, revision)
goa_libversion = '0.0.0'
goa_backend_libversion = '1.0.0'

goa_buildtype = get_option('buildtype')
cc = meson.get_compiler('c')

config_h = configuration_data()

# quoted defines
config_h.set_quoted('PACKAGE_VERSION', goa_version)
# i18n
config_h.set_quoted('GETTEXT_PACKAGE', meson.project_name())

# version values
config_h.set('GOA_MAJOR_VERSION', goa_major_version)
config_h.set('GOA_MINOR_VERSION', goa_minor_version)
config_h.set('GOA_MICRO_VERSION', goa_micro_version)

# compiler flags
compiler_flags = []

if goa_buildtype.contains('debug')
  compiler_flags += cc.get_supported_arguments([
    '-Werror=format=2',
    '-Werror=implicit-function-declaration',
    '-Werror=init-self',
    '-Werror=missing-include-dirs',
    '-Werror=missing-prototypes',
    '-Werror=pointer-arith',
    '-Werror=return-type',
    '-Wnested-externs',
    '-Wstrict-prototypes'
  ])
endif

add_project_arguments(compiler_flags, language: 'c')

glib_req_version = '>= 2.52'

gio_dep = dependency('gio-2.0', version: glib_req_version)
gio_unix_dep = dependency('gio-unix-2.0', version: glib_req_version)
glib_dep = dependency('glib-2.0', version: glib_req_version)

# Check for dbus service dir
dbus_dep = dependency('dbus-1')
dbus_service_dir = dbus_dep.get_pkgconfig_variable('session_bus_services_dir',
                                                   define_variable: ['datadir', goa_datadir])

# Libraries
# introspection support
enable_introspection = get_option('introspection')
if enable_introspection
  # Check do enough recent version
  dependency('gobject-introspection-1.0', version: '>= 0.6.2')
endif

enable_goabackend = get_option('goabackend')
if enable_goabackend
  gtk_dep = dependency('gtk+-3.0', version: '>= 3.19.12')
  json_glib_dep = dependency('json-glib-1.0')
  libsecret_dep = dependency('libsecret-1')
  libsoup_dep = dependency('libsoup-2.4', version: '>= 2.42')
  libxml_dep = dependency('libxml-2.0')
  rest_dep = dependency('rest-0.7')
  webkit_gtk_dep = dependency('webkit2gtk-4.0', version: '>= 2.12.0')
endif
config_h.set('GOA_BACKEND_ENABLED', enable_goabackend)

enable_inspector = get_option('inspector')
config_h.set('GOA_INSPECTOR_ENABLED', enable_inspector)

template_file = get_option('template_file')
config_h.set_quoted('GOA_TEMPLATE_FILE', template_file)

enable_vapi = get_option('vapi')

# Microsoft Exchange
config_h.set_quoted('GOA_EXCHANGE_NAME', 'exchange')
enable_exchange = get_option('exchange')
config_h.set('GOA_EXCHANGE_ENABLED', enable_exchange)

# Facebook
config_h.set_quoted('GOA_FACEBOOK_NAME', 'facebook')

facebook_client_id = get_option('facebook_client_id')
config_h.set_quoted('GOA_FACEBOOK_CLIENT_ID', facebook_client_id)

enable_facebook = get_option('facebook')
config_h.set('GOA_FACEBOOK_ENABLED', enable_facebook)

# Flickr
config_h.set_quoted('GOA_FLICKR_NAME', 'flickr')

flickr_consumer_key = get_option('flickr_consumer_key')
config_h.set_quoted('GOA_FLICKR_CONSUMER_KEY', flickr_consumer_key)

flickr_consumer_secret = get_option('flickr_consumer_secret')
config_h.set_quoted('GOA_FLICKR_CONSUMER_SECRET', flickr_consumer_secret)

enable_flickr = get_option('flickr')
config_h.set('GOA_FLICKR_ENABLED', enable_flickr)

# Foursquare
config_h.set_quoted('GOA_FOURSQUARE_NAME', 'foursquare')

foursquare_client_id = get_option('foursquare_client_id')
config_h.set_quoted('GOA_FOURSQUARE_CLIENT_ID', foursquare_client_id)

enable_foursquare = get_option('foursquare')
config_h.set('GOA_FOURSQUARE_ENABLED', enable_foursquare)

# Google
config_h.set_quoted('GOA_GOOGLE_NAME', 'google')

google_client_id = get_option('google_client_id')
config_h.set_quoted('GOA_GOOGLE_CLIENT_ID', google_client_id)

google_client_secret = get_option('google_client_secret')
config_h.set_quoted('GOA_GOOGLE_CLIENT_SECRET', google_client_secret)

enable_google = get_option('google')
config_h.set('GOA_GOOGLE_ENABLED', enable_google)

# IMAP/SMTP
config_h.set_quoted('GOA_IMAP_SMTP_NAME', 'imap_smtp')

enable_imap_smtp = get_option('imap_smtp')
config_h.set('GOA_IMAP_SMTP_ENABLED', enable_imap_smtp)

# Kerberos
config_h.set_quoted('GOA_KERBEROS_NAME', 'kerberos')

enable_kerberos = get_option('kerberos')
if enable_kerberos
  gcr_dep = dependency('gcr-3')
  krb5_dep = dependency('krb5')

  config_h.set('GCR_API_SUBJECT_TO_CHANGE', true)
endif
config_h.set('GOA_KERBEROS_ENABLED', enable_kerberos)

# Media Server
config_h.set_quoted('GOA_MEDIA_SERVER_NAME', 'media-server')

enable_media_server = get_option('media_server')
config_h.set('GOA_MEDIA_SERVER_ENABLED', enable_media_server)

# Last.fm
config_h.set_quoted('GOA_LASTFM_NAME', 'lastfm')

lastfm_client_id = get_option('lastfm_client_id')
config_h.set_quoted('GOA_LASTFM_CLIENT_ID', lastfm_client_id)

lastfm_client_secret = get_option('lastfm_client_secret')
config_h.set_quoted('GOA_LASTFM_CLIENT_SECRET', lastfm_client_secret)

enable_lastfm = get_option('lastfm')
config_h.set('GOA_LASTFM_ENABLED', enable_lastfm)

# ownCloud
config_h.set_quoted('GOA_OWNCLOUD_NAME', 'owncloud')
enable_owncloud = get_option('owncloud')
config_h.set('GOA_OWNCLOUD_ENABLED', enable_owncloud)

# Pocket
config_h.set_quoted('GOA_POCKET_NAME', 'pocket')

pocket_client_id = get_option('pocket_client_id')
config_h.set_quoted('GOA_POCKET_CLIENT_ID', pocket_client_id)

enable_pocket = get_option('pocket')
config_h.set('GOA_POCKET_ENABLED', enable_pocket)

# Windows Live
config_h.set_quoted('GOA_WINDOWS_LIVE_NAME', 'windows_live')

windows_live_client_id = get_option('windows_live_client_id')
config_h.set_quoted('GOA_WINDOWS_LIVE_CLIENT_ID', windows_live_client_id)

enable_windows_live = get_option('windows_live')
config_h.set('GOA_WINDOWS_LIVE_ENABLED', enable_windows_live)

# Optional timerfd support
timerfd_support_src = '''
  #include <sys/timerfd.h>
  #include <unistd.h>
  int
  main (void)
  {
    struct itimerspec timer_spec = { 0 };
    timerfd_settime (timerfd_create (CLOCK_MONOTONIC, TFD_CLOEXEC),
                     TFD_TIMER_ABSTIME,
                     &timer_spec,
                     NULL);

    return 0;
  };
'''

if cc.compiles(timerfd_support_src, name: 'timerfd support')
  config_h.set('HAVE_TIMERFD', true)

  # libc headers tend to trail kernel support
  # so compensate if necessary
  timerfd_cancel_on_set_support_src = '''
    #include <sys/timerfd.h>
    #include <unistd.h>
    int
    main (void)
    {
      struct itimerspec timer_spec = { 0 };
      timerfd_settime (timerfd_create (CLOCK_MONOTONIC, TFD_CLOEXEC),
                       TFD_TIMER_ABSTIME | TFD_TIMER_CANCEL_ON_SET,
                       &timer_spec,
                       NULL);

      return 0;
    };
  '''

  if not cc.compiles(timerfd_cancel_on_set_support_src, name: 'timerfd cancel-on-set support')
    config_h.set('TFD_TIMER_CANCEL_ON_SET', '(1 << 1)')
  endif
endif

configure_file(
  output: 'config.h',
  configuration: config_h
)

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

top_inc = include_directories('.')

subdir('data')
subdir('src')

enable_gtk_doc = get_option('gtk_doc')
enable_man = get_option('man')
if enable_goabackend and (enable_gtk_doc or enable_man)
  subdir('doc')
endif

subdir('po')

meson.add_install_script(
  'meson_post_install.py',
  goa_datadir,
  enable_goabackend ? 'compile-schemas' : ''
)

output = '\n        gnome-online-accounts ' + goa_version + '\n'
output += '        =============================\n'
output += '        prefix:                         ' + goa_prefix + '\n'
output += '        libdir:                         ' + goa_libdir + '\n'
output += '        libexecdir:                     ' + goa_libexecdir + '\n'
output += '        datadir:                        ' + goa_datadir + '\n'
output += '        compiler:                       ' + cc.get_id() + '\n'
output += '        cflags:                         ' + ' '.join(compiler_flags) + '\n\n'
output += '        goabackend:                     ' + enable_goabackend.to_string() + '\n'
output += '        inspector:                      ' + enable_inspector.to_string() + '\n'
output += '        introspection:                  ' + enable_introspection.to_string() + '\n'
output += '        template file:                  ' + template_file + '\n\n'
output += '        Facebook provider:              ' + enable_facebook.to_string()
if enable_facebook
  output += ' (OAuth 2.0, id:@0@)'.format(facebook_client_id)
endif
output += '\n'
output += '        Flickr provider:                ' + enable_flickr.to_string()
if enable_flickr
  output += ' (OAuth 1.0, key:@0@)'.format(flickr_consumer_key)
endif
output += '\n'
output += '        Foursquare provider:            ' + enable_foursquare.to_string()
if enable_foursquare
  output += ' (id:@0@)'.format(foursquare_client_id)
endif
output += '\n'
output += '        Google provider:                ' + enable_google.to_string()
if enable_google
  output += ' (OAuth 2.0, id:@0@ secret:@1@)'.format(google_client_id, google_client_secret)
endif
output += '\n'
output += '        IMAP/SMTP provider:             ' + enable_imap_smtp.to_string() + '\n'
output += '        Kerberos provider:              ' + enable_kerberos.to_string() + '\n'
output += '        Last.fm provider:               ' + enable_lastfm.to_string()
if enable_lastfm
  output += ' (id:@0@ secret:@1@)'.format(lastfm_client_id, lastfm_client_secret)
endif
output += '\n'
output += '        Media Server provider:          ' + enable_media_server.to_string() + '\n'
output += '        Microsoft Exchange provider:    ' + enable_exchange.to_string() + '\n'
output += '        ownCloud provider:              ' + enable_owncloud.to_string() + '\n'
output += '        Pocket provider:                ' + enable_pocket.to_string()
if enable_pocket
  output += ' (id:@0@)'.format(pocket_client_id)
endif
output += '\n'
output += '        Windows Live provider:          ' + enable_windows_live.to_string()
if enable_windows_live
  output += ' (OAuth 2.0, id:@0@)'.format(windows_live_client_id)
endif
output += '\n\n'
output += '        Build type:                     ' + goa_buildtype + '\n'
output += '        Vala:                           ' + enable_vapi.to_string() + '\n'
output += '        Build api docs:                 ' + enable_gtk_doc.to_string() + '\n'
output += '        Build man:                      ' + enable_man.to_string()
message(output)
