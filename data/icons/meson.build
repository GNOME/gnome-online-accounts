icon_data = [
  'goa-account-facebook.png',
  'goa-account-flickr.png',
  'goa-account-foursquare.png',
  'goa-account-google.png',
  'goa-account-msn.png',
  'goa-account-owncloud.png',
  'goa-account.png',
]

icon_data_256 = ['goa-account.png']

icons = [
  ['16x16', icon_data],
  ['22x22', icon_data],
  ['24x24', icon_data],
  ['32x32', icon_data],
  ['48x48', icon_data],
  ['96x96', icon_data],
  ['256x256', icon_data_256]
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
