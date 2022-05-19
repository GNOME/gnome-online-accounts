src_inc = include_directories('.')

common_incs = [top_inc, src_inc]

subdir('goa')

if enable_goabackend
  if enable_fedora or enable_kerberos
    subdir('goaidentity')
  endif

  subdir('goabackend')
  subdir('daemon')
endif

subdir('examples')
