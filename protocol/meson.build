wayland_scanner = dependency('wayland-scanner', native: true, version: '>=1.14.91')
wayland_scanner = find_program(wayland_scanner.get_variable('wayland_scanner'), native: true)

wl_protocol_dir = wayland_protos.get_variable('pkgdatadir')

wayland_scanner_code = generator(
	wayland_scanner,
	output: '@BASENAME@-protocol.c',
	arguments: ['private-code', '@INPUT@', '@OUTPUT@'],
)

wayland_scanner_client = generator(
	wayland_scanner,
	output: '@BASENAME@-client-protocol.h',
	arguments: ['client-header', '@INPUT@', '@OUTPUT@'],
)

client_protocols = [
	wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml',
	wl_protocol_dir / 'unstable/xdg-output/xdg-output-unstable-v1.xml',
	'wlr-layer-shell-unstable-v1.xml',
]

client_protos_src = []
client_protos_headers = []

foreach xml : client_protocols
	client_protos_src += wayland_scanner_code.process(xml)
	client_protos_headers += wayland_scanner_client.process(xml)
endforeach

lib_client_protos = static_library(
	'client_protos',
	client_protos_src + client_protos_headers,
	dependencies: [wayland_client]
) # for the include directory

client_protos = declare_dependency(
	link_with: lib_client_protos,
	sources: client_protos_headers,
)
