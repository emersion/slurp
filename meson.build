project(
	'slurp',
	'c',
	version: '0.0.0',
	license: 'MIT',
	meson_version: '>=0.43.0',
	default_options: [
		'c_std=c11',
		'warning_level=2',
		'werror=true',
	],
)

add_project_arguments('-Wno-unused-parameter', language: 'c')

slurp_inc = include_directories('include')

cairo = dependency('cairo')
wayland_client = dependency('wayland-client')
wayland_protos = dependency('wayland-protocols', version: '>=1.14')

subdir('protocol')

executable(
	'slurp',
	files([
		'main.c',
		'pool-buffer.c',
		'render.c',
	]),
	dependencies: [
		cairo,
		client_protos,
		wayland_client,
	],
	include_directories: [slurp_inc],
	install: true,
)

scdoc = find_program('scdoc', required: false)

if scdoc.found()
	sh = find_program('sh')

	man_pages = [] # TODO

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
