if enable_gtk_doc
  version_conf = configuration_data()
  version_conf.set('VERSION', goa_version)

  version_xml = 'version.xml'

  configure_file(
    input: version_xml + '.in',
    output: version_xml,
    configuration: version_conf
  )

  ignored_headers = [
    # srcdir/goabackend
    'gconstructor.h',
    'goabackendenums-priv.h',
    'goadlnaservermanager.h',
    'goaewsclient.h',
    'goaexchangeprovider.h',
    'goafedoraprovider.h',
    'goagoogleprovider.h',
    'goahttpclient.h',
    'goaimapauthlogin.h',
    'goaimapsmtpprovider.h',
    'goakerberosprovider.h',
    'goamailauth.h',
    'goamailclient.h',
    'goaoauth2provider-priv.h',
    'goaoauth2provider-web-extension.h',
    'goaoauth2provider-web-view.h',
    'goaoauth2provider.h',
    'goaobjectskeletonutils.h',
    'goaowncloudprovider.h',
    'goaprovider-priv.h',
    'goarestproxy.h',
    'goasmtpauth.h',
    'goasouplogger.h',
    'goautils.h',
    'goawebextension.h',
    'goawindowsliveprovider.h',
  ]

  expand_content_files = files(
    'goa-daemon.xml',
    'goa-overview.xml'
  )

  gnome.gtkdoc(
    goa_name,
    main_xml: goa_name + '-docs.xml',
    src_dir: [goa_inc, goabackend_inc],
    ignore_headers: ignored_headers,
    dependencies: libgoa_backend_dep,
    mkdb_args: '--name-space=' + goa_name,
    fixxref_args: '--html-dir=' + join_paths(goa_prefix, gnome.gtkdoc_html_dir(goa_name)),
    gobject_typesfile: files(goa_name + '.types'),
    expand_content_files: expand_content_files,
    install: true,
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
