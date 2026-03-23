datadir = join_paths(get_option('prefix'), get_option('datadir'))
applicationsdir = join_paths(datadir, 'applications')
iconscalabledir = join_paths(datadir, 'icons', 'hicolor', 'scalable', 'apps')
pixmapsdir = join_paths(datadir, 'pixmaps')

executable('victus-control',
  sources: ['src/main.cpp', 'src/keyboard.cpp', 'src/fan.cpp', 'src/about.cpp', 'src/socket.cpp'],
  dependencies: [dependency('gtk4'), dependency('threads')],
  cpp_args: ['-DDATADIR="' + datadir + '"'],
  install: true,
  install_dir: get_option('bindir'))

install_data(
	'victus-control.desktop',
	install_dir: applicationsdir
)

install_data(
	'victus-icon.svg',
	install_dir: iconscalabledir
)

install_data(
	'victus-icon.svg',
	install_dir: pixmapsdir
)
