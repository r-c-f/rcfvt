project('rcfvt', 'c')
vtedep = dependency('vte-2.91')

canberradep = dependency('libcanberra', required : false)
notifydep = dependency('libnotify', required : false)
cc = meson.get_compiler('c')
if cc.has_function('dlopen')
  dldep = []
elif cc.has_function('dlopen', args: '-ldl')
  dldep = cc.find_library('dl')
else
  error('no dlopen support')
endif

executable('rcfvt', ['src/main.c','src/config.c', 'src/ipc.c', 'src/ca_plugin.c', 'src/notify_plugin.c'], install: true, dependencies : [vtedep, dldep])
if canberradep.found()
  shared_library('rcfvtca', 'src/rcfvtca.c', install: true, dependencies : [vtedep, canberradep])
endif
if notifydep.found()
  shared_library('rcfvtnotify', 'src/rcfvtnotify.c', install: true, dependencies : [vtedep, notifydep])
endif

