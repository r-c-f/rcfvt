project('rcfvt', 'c')
vtedep = dependency('vte-2.91')

canberradep = dependency('libcanberra', required : false)
cc = meson.get_compiler('c')
dldep = cc.find_library('dl')

executable('rcfvt', ['src/main.c','src/config.c', 'src/ipc.c', 'src/ca_plugin.c'], install: true, dependencies : [vtedep, dldep])
if canberradep.found()
  shared_library('rcfvtca', 'src/rcfvtca.c', install: true, dependencies : [vtedep, canberradep])
endif
