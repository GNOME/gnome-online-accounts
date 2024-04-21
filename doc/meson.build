if get_option('documentation') and get_option('introspection')
  gdbus_codegen = find_program('gdbus-codegen')
  dbus_md = custom_target('dbus-md',
    input: dbus_ifaces,
    output: [
      'dbus-org.gnome.OnlineAccounts.Account.md',
      'dbus-org.gnome.OnlineAccounts.OAuthBased.md',
      'dbus-org.gnome.OnlineAccounts.OAuth2Based.md',
      'dbus-org.gnome.OnlineAccounts.PasswordBased.md',
      'dbus-org.gnome.OnlineAccounts.Manager.md',
      'dbus-org.gnome.OnlineAccounts.Mail.md',
      'dbus-org.gnome.OnlineAccounts.Calendar.md',
      'dbus-org.gnome.OnlineAccounts.Contacts.md',
      'dbus-org.gnome.OnlineAccounts.Chat.md',
      'dbus-org.gnome.OnlineAccounts.Documents.md',
      'dbus-org.gnome.OnlineAccounts.Maps.md',
      'dbus-org.gnome.OnlineAccounts.Music.md',
      'dbus-org.gnome.OnlineAccounts.Photos.md',
      'dbus-org.gnome.OnlineAccounts.Files.md',
      'dbus-org.gnome.OnlineAccounts.Exchange.md',
      'dbus-org.gnome.OnlineAccounts.MediaServer.md',
      'dbus-org.gnome.OnlineAccounts.Ticketing.md',
      'dbus-org.gnome.OnlineAccounts.Todo.md',
      'dbus-org.gnome.OnlineAccounts.ReadLater.md',
      'dbus-org.gnome.OnlineAccounts.Printers.md',
    ],
    command: [
      gdbus_codegen,
      '--generate-md', 'dbus',
      '--output-directory', '@OUTDIR@',
      '@INPUT@',
    ],
    build_by_default: true,
  )

  gidocgen = find_program('gi-docgen', required: true)
  goa_include_dir = join_paths(meson.project_build_root(), 'goa')
  goa_docdir = join_paths(goa_datadir, 'doc')

  toml_conf = configuration_data()
  toml_conf.set('version', meson.project_version())
  goa_toml = configure_file(
            input: 'goa.toml.in',
           output: 'goa.toml',
    configuration: toml_conf
  )

  custom_target('goa-doc',
    input: [goa_toml, libgoa_gir[0]],
    output: meson.project_name(),
    command: [
      gidocgen,
      'generate',
      '--quiet',
      '--fatal-warnings',
      '--add-include-path=@0@'.format(goa_include_dir),
      '--config=@INPUT0@',
      '--output-dir=@OUTPUT@',
      '--content-dir=@0@'.format(meson.current_build_dir()),
      '--content-dir=@0@'.format(meson.current_source_dir()),
      '@INPUT1@',
    ],
    depends: [libgoa_gir[0], dbus_md],
    depend_files: [
      'overview.md',
      'services.md',
      'goals.md',
    ],
    build_by_default: true,
    install: true,
    install_dir: goa_docdir,
  )
endif

if enable_man
  xsltproc_cmd = [
    find_program('xsltproc'),
    '--output', '@OUTPUT@',
    '--nonet',
    '--stringparam', 'man.output.quietly', '1',
    '--stringparam', 'funcsynopsis.style', 'ansi',
    'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
    '@INPUT@'
  ]

  man = 'goa-daemon'

  custom_target(
    man + '.8',
    input: man + '.xml',
    output: man + '.8',
    command: xsltproc_cmd,
    install: true,
    install_dir: join_paths(goa_mandir, 'man8')
  )
endif
