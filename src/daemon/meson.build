name = 'goa-daemon'

sources = files(
  'goadaemon.c',
  'main.c'
)

deps = [
  libgoa_backend_dep,
  rest_dep
]

cflags = [
  '-DG_LOG_DOMAIN="@0@"'.format(name),
  '-DGOA_API_IS_SUBJECT_TO_CHANGE',
  '-DGOA_BACKEND_API_IS_SUBJECT_TO_CHANGE',
  '-DGOA_BACKEND_COMPILATION',
  '-DPACKAGE_LOCALE_DIR="@0@"'.format(goa_localedir),
]

executable(
  name,
  sources,
  include_directories: common_incs,
  dependencies: deps,
  c_args: cflags,
  install: true,
  install_dir: goa_libexecdir
)
