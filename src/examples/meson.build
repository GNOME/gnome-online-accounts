programs = [
  # program name, deps
  ['list-accounts', libgoa_dep],
  ['lastfm-shout', [libgoa_dep, rest_dep]]
]

if enable_goabackend
  programs += [
    ['introspect-providers', libgoa_backend_dep],
    ['list-providers', libgoa_backend_dep],
    ['add-pocket', [libgoa_dep, rest_dep]]
  ]
endif

foreach program: programs
  executable(
    program[0],
    program[0] + '.c',
    include_directories: common_incs,
    dependencies: program[1]
  )
endforeach
