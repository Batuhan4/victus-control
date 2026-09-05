executable('victus-hotkeyd',
  sources: ['src/main.cpp'],
  install: true,
  install_dir: get_option('bindir'))

# A user unit, not a system one: it launches a GUI, so it has to live in the
# session that owns the display.
install_data(
	'victus-hotkeyd.service',
	install_dir: '/usr/lib/systemd/user'
)

install_data(
	'99-victus-hotkey.rules',
	install_dir: '/etc/udev/rules.d'
)
