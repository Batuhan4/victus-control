executable('victus-control',
  sources: ['src/main.cpp', 'src/keyboard.cpp', 'src/fan.cpp', 'src/about.cpp', 'src/socket.cpp'],
  dependencies: [dependency('gtk4'), dependency('threads')],
  install: true,
  install_dir: get_option('bindir'))

install_data(
	'victus-control.desktop',
	install_dir: '/usr/share/applications'
)

install_data(
	'victus-icon.svg', 
	install_dir: join_paths(get_option('prefix'), 'share', 'icons', 'hicolor', '48x48', 'apps')
)
