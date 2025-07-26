subdir('icons')

dbus_ifaces = files('dbus-interfaces.xml')

if enable_goabackend
  services = ['org.gnome.OnlineAccounts.service']
  if enable_kerberos
    services += 'org.gnome.Identity.service'
  endif

  service_conf = configuration_data()
  service_conf.set('libexecdir', goa_libexecdir)

  foreach service: services
    configure_file(
      input: service + '.in',
      output: service,
      configuration: service_conf,
      install: true,
      install_dir: dbus_service_dir
    )
  endforeach

  oauth2_schemes = ['x-scheme-handler/goa-oauth2']
  if enable_google
    parts = google_client_id.split('.')
    reversed = []
    foreach part : parts
      reversed = [part] + reversed
    endforeach
    google_scheme = '.'.join(reversed)
    oauth2_schemes += 'x-scheme-handler/@0@'.format(google_scheme)
  endif

  handler_conf = configuration_data()
  handler_conf.set('libexecdir', goa_libexecdir)
  handler_conf.set('oauth2_schemes', ';'.join(oauth2_schemes))
  configure_file(
    input: 'org.gnome.OnlineAccounts.OAuth2.desktop.in',
    output: 'org.gnome.OnlineAccounts.OAuth2.desktop',
    configuration: handler_conf,
    install: true,
    install_dir: goa_datadir / 'applications'
  )
endif
