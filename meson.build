project('rcfvt', 'c')
vtedep = dependency('vte-2.91')

canberradep = dependency('libcanberra', required : false)
if canberradep.found()
  add_global_arguments('-DHAVE_CANBERRA=1', language : 'c')
endif

executable('rcfvt', ['src/main.c','src/config.c', 'src/ipc.c'], install: true, dependencies : [vtedep, canberradep])

