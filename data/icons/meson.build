icon_scalable_data = [
  'goa-account.svg',
  'goa-account-exchange.svg',
  'goa-account-fedora.svg',
  'goa-account-google.svg',
  'goa-account-ms365.svg',
  'goa-account-owncloud.svg',
]

icon_symbolic_data = [
  'goa-account-symbolic.svg',
  'goa-account-exchange-symbolic.svg',
  'goa-account-google-symbolic.svg',
  'goa-account-owncloud-symbolic.svg',
]

icons = [
  ['scalable', icon_scalable_data],
  ['symbolic', icon_symbolic_data],
]

foreach icon: icons
  icon_dir = join_paths(goa_datadir, 'icons', 'hicolor', icon[0], 'apps')

  foreach data: icon[1]
    install_data(
      join_paths(icon[0], data),
      install_dir: icon_dir
    )
  endforeach
endforeach
