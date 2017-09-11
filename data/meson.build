subdir('icons')

dbus_ifaces = files('dbus-interfaces.xml')

if enable_goabackend
  install_data(
    'org.gnome.online-accounts.gschema.xml',
    install_dir: join_paths(goa_datadir, 'glib-2.0', 'schemas')
  )

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
endif
