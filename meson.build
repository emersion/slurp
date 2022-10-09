project(
	'slurp',
	'c',
	version: '1.3.2',
	license: 'MIT',
	meson_version: '>=0.48.0',
	default_options: ['c_std=c11', 'warning_level=2', 'werror=true'],
)

add_project_arguments('-Wno-unused-parameter', language: 'c')

slurp_inc = include_directories('include')

cc = meson.get_compiler('c')

cairo = dependency('cairo')
realtime = cc.find_library('rt')
wayland_client = dependency('wayland-client')
wayland_cursor = dependency('wayland-cursor')
wayland_protos = dependency('wayland-protocols', version: '>=1.14')
xkbcommon = dependency('xkbcommon')

subdir('protocol')

executable(
	'slurp',
	files([
		'main.c',
		'pool-buffer.c',
		'render.c',
		'box.c'
	]),
	dependencies: [
		cairo,
		client_protos,
		realtime,
		wayland_client,
		wayland_cursor,
		xkbcommon,
	],
	include_directories: [slurp_inc],
	install: true,
)

scdoc = find_program('scdoc', required: get_option('man-pages'))

if scdoc.found()
	sh = find_program('sh')

	man_pages = ['slurp.1.scd']

	mandir = get_option('mandir')

	foreach src : man_pages
		topic = src.split('.')[0]
		section = src.split('.')[1]
		output = '@0@.@1@'.format(topic, section)

		custom_target(
			output,
			input: src,
			output: output,
			command: [
				sh, '-c', '@0@ < @INPUT@ > @1@'.format(scdoc.path(), output)
			],
			install: true,
			install_dir: '@0@/man@1@'.format(mandir, section)
		)
	endforeach
endif
