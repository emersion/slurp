#define _POSIX_C_SOURCE 2
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <linux/input-event-codes.h>

#include "slurp.h"
#include "render.h"

static void noop() {
	// This space intentionally left blank
}


static void set_output_dirty(struct slurp_output *output);

bool box_intersect(const struct slurp_box *a, const struct slurp_box *b) {
	return a->x < b->x + b->width &&
		a->x + a->width > b->x &&
		a->y < b->y + b->height &&
		a->height + a->y > b->y;
}

static struct slurp_output *output_from_surface(struct slurp_state *state,
	struct wl_surface *surface);

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct slurp_seat *seat = data;
	struct slurp_output *output = output_from_surface(seat->state, surface);
	if (output == NULL) {
		return;
	}
	// TODO: handle multiple overlapping outputs
	seat->current_output = output;

	seat->x = wl_fixed_to_int(surface_x) + seat->current_output->logical_geometry.x;
	seat->y = wl_fixed_to_int(surface_y) + seat->current_output->logical_geometry.y;

	wl_surface_set_buffer_scale(seat->cursor_surface, output->scale);
	wl_surface_attach(seat->cursor_surface,
			wl_cursor_image_get_buffer(output->cursor_image), 0, 0);
	wl_pointer_set_cursor(wl_pointer, serial, seat->cursor_surface,
			output->cursor_image->hotspot_x / output->scale,
			output->cursor_image->hotspot_y / output->scale);
	wl_surface_commit(seat->cursor_surface);
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	struct slurp_seat *seat = data;

	// TODO: handle multiple overlapping outputs
	seat->current_output = NULL;
}

static void seat_set_outputs_dirty(struct slurp_seat *seat) {
	struct slurp_box box;
	seat_get_box(seat, &box);
	struct slurp_output *output;
	wl_list_for_each(output, &seat->state->outputs, link) {
		if (box_intersect(&output->logical_geometry, &box)) {
			set_output_dirty(output);
		}
	}
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct slurp_seat *seat = data;
	// the places the cursor moved away from are also dirty
	if (seat->button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
		seat_set_outputs_dirty(seat);
	}

	seat->x = wl_fixed_to_int(surface_x) + seat->current_output->logical_geometry.x;
	seat->y = wl_fixed_to_int(surface_y) + seat->current_output->logical_geometry.y;

	if (seat->button_state == WL_POINTER_BUTTON_STATE_PRESSED) {
		seat_set_outputs_dirty(seat);
	}
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button,
		uint32_t button_state) {
	struct slurp_seat *seat = data;
	struct slurp_state *state = seat->state;

	seat->button_state = button_state;

	switch (button_state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		if (state->single_point) {
			state->result.x = seat->x;
			state->result.y = seat->y;
			state->result.width = state->result.height = 1;
			state->running = false;
		} else {
			seat->pressed_x = seat->x;
			seat->pressed_y = seat->y;
		}
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		if (!state->single_point) {
			seat_get_box(seat, &state->result);
			state->running = false;
		}
		break;
	}
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = noop,
};

static int min(int a, int b) {
	return (a < b) ? a : b;
}

void seat_get_box(struct slurp_seat *seat, struct slurp_box *result) {
	result->x = min(seat->pressed_x, seat->x);
	result->y = min(seat->pressed_y, seat->y);
	result->width = abs(seat->x - seat->pressed_x);
	result->height = abs(seat->y - seat->pressed_y);
}

static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
		uint32_t serial, uint32_t time, uint32_t key, uint32_t key_state) {
	struct slurp_seat *seat = data;
	struct slurp_state *state = seat->state;
	if (key_state == WL_KEYBOARD_KEY_STATE_PRESSED) {
		if (key == KEY_ESC) {
			state->running = false;
		}
	}
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = noop,
	.enter = noop,
	.leave = noop,
	.key = keyboard_handle_key,
	.modifiers = noop,
};


static void seat_handle_capabilities(void *data, struct wl_seat *wl_seat,
		uint32_t capabilities) {
	struct slurp_seat *seat = data;

	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		seat->wl_pointer = wl_seat_get_pointer(wl_seat);
		wl_pointer_add_listener(seat->wl_pointer, &pointer_listener, seat);
	}
	if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
		seat->wl_keyboard = wl_seat_get_keyboard(wl_seat);
		wl_keyboard_add_listener(seat->wl_keyboard, &keyboard_listener, seat);
	}
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
};

static void create_seat(struct slurp_state *state, struct wl_seat *wl_seat) {
	struct slurp_seat *seat = calloc(1, sizeof(struct slurp_seat));
	if (seat == NULL) {
		fprintf(stderr, "allocation failed\n");
		return;
	}
	seat->state = state;
	seat->wl_seat = wl_seat;
	wl_list_insert(&state->seats, &seat->link);
	wl_seat_add_listener(wl_seat, &seat_listener, seat);
}

static void destroy_seat(struct slurp_seat *seat) {
	wl_list_remove(&seat->link);
	wl_surface_destroy(seat->cursor_surface);
	if (seat->wl_pointer) {
		wl_pointer_destroy(seat->wl_pointer);
	}
	if (seat->wl_keyboard) {
		wl_keyboard_destroy(seat->wl_keyboard);
	}
	wl_seat_destroy(seat->wl_seat);
	free(seat);
}

static void output_handle_geometry(void *data, struct wl_output *wl_output,
		int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
		int32_t subpixel, const char *make, const char *model,
		int32_t transform) {
	struct slurp_output *output = data;

	output->geometry.x = x;
	output->geometry.y = y;
}

static void output_handle_mode(void *data, struct wl_output *wl_output,
		uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
	struct slurp_output *output = data;
	if ((flags & WL_OUTPUT_MODE_CURRENT) == 0) {
		return;
	}
	output->geometry.width = width;
	output->geometry.height = height;
}

static void output_handle_scale(void *data, struct wl_output *wl_output,
		int32_t scale) {
	struct slurp_output *output = data;

	output->scale = scale;
}

static const struct wl_output_listener output_listener = {
	.geometry = output_handle_geometry,
	.mode = output_handle_mode,
	.done = noop,
	.scale = output_handle_scale,
};

static void xdg_output_handle_logical_position(void *data,
		struct zxdg_output_v1 *xdg_output, int32_t x, int32_t y) {
	struct slurp_output *output = data;
	output->logical_geometry.x = x;
	output->logical_geometry.y = y;
}
static void xdg_output_handle_logical_size(void *data,
		struct zxdg_output_v1 *xdg_output, int32_t width, int32_t height) {
	struct slurp_output *output = data;
	output->logical_geometry.width = width;
	output->logical_geometry.height = height;
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = xdg_output_handle_logical_position,
	.logical_size = xdg_output_handle_logical_size,
	.done = noop,
	.name = noop,
	.description = noop,
};

static void create_output(struct slurp_state *state,
		struct wl_output *wl_output) {
	struct slurp_output *output = calloc(1, sizeof(struct slurp_output));
	if (output == NULL) {
		fprintf(stderr, "allocation failed\n");
		return;
	}
	output->wl_output = wl_output;
	output->state = state;
	output->scale = 1;
	wl_list_insert(&state->outputs, &output->link);

	wl_output_add_listener(wl_output, &output_listener, output);
}

static void destroy_output(struct slurp_output *output) {
	if (output == NULL) {
		return;
	}
	wl_list_remove(&output->link);
	finish_buffer(&output->buffers[0]);
	finish_buffer(&output->buffers[1]);
	wl_cursor_theme_destroy(output->cursor_theme);
	zwlr_layer_surface_v1_destroy(output->layer_surface);
	if (output->xdg_output) {
		zxdg_output_v1_destroy(output->xdg_output);
	}
	wl_surface_destroy(output->surface);
	if (output->frame_callback) {
		wl_callback_destroy(output->frame_callback);
	}
	wl_output_destroy(output->wl_output);
	free(output);
}

static const struct wl_callback_listener output_frame_listener;

static void send_frame(struct slurp_output *output) {
	struct slurp_state *state = output->state;

	if (!output->configured) {
		return;
	}

	int32_t buffer_width = output->width * output->scale;
	int32_t buffer_height = output->height * output->scale;

	output->current_buffer = get_next_buffer(state->shm, output->buffers,
		buffer_width, buffer_height);
	if (output->current_buffer == NULL) {
		return;
	}

	render(output);

	// Schedule a frame in case the output becomes dirty again
	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback,
		&output_frame_listener, output);

	wl_surface_attach(output->surface, output->current_buffer->buffer, 0, 0);
	wl_surface_damage(output->surface, 0, 0, output->width, output->height);
	wl_surface_set_buffer_scale(output->surface, output->scale);
	wl_surface_commit(output->surface);
	output->dirty = false;
}

static void output_frame_handle_done(void *data, struct wl_callback *callback,
		uint32_t time) {
	struct slurp_output *output = data;

	wl_callback_destroy(callback);
	output->frame_callback = NULL;

	if (output->dirty) {
		send_frame(output);
	}
}

static const struct wl_callback_listener output_frame_listener = {
	.done = output_frame_handle_done,
};

static void set_output_dirty(struct slurp_output *output) {
	output->dirty = true;
	if (output->frame_callback) {
		return;
	}

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback,
		&output_frame_listener, output);
	wl_surface_commit(output->surface);
}

static struct slurp_output *output_from_surface(struct slurp_state *state,
		struct wl_surface *surface) {
	struct slurp_output *output;
	wl_list_for_each(output, &state->outputs, link) {
		if (output->surface == surface) {
			return output;
		}
	}
	return NULL;
}


static void layer_surface_handle_configure(void *data,
		struct zwlr_layer_surface_v1 *surface,
		uint32_t serial, uint32_t width, uint32_t height) {
	struct slurp_output *output = data;

	output->configured = true;
	output->width = width;
	output->height = height;

	zwlr_layer_surface_v1_ack_configure(surface, serial);
	send_frame(output);
}

static void layer_surface_handle_closed(void *data,
		struct zwlr_layer_surface_v1 *surface) {
	struct slurp_output *output = data;
	destroy_output(output);
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
	.configure = layer_surface_handle_configure,
	.closed = layer_surface_handle_closed,
};


static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	struct slurp_state *state = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0) {
		state->compositor = wl_registry_bind(registry, name,
			&wl_compositor_interface, 4);
	} else if (strcmp(interface, wl_shm_interface.name) == 0) {
		state->shm = wl_registry_bind(registry, name,
			&wl_shm_interface, 1);
	} else if (strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0) {
		state->layer_shell = wl_registry_bind(registry, name,
			&zwlr_layer_shell_v1_interface, 1);
	} else if (strcmp(interface, wl_seat_interface.name) == 0) {
		struct wl_seat *wl_seat =
			wl_registry_bind(registry, name, &wl_seat_interface, 1);
		create_seat(state, wl_seat);
	} else if (strcmp(interface, wl_output_interface.name) == 0) {
		struct wl_output *wl_output =
			wl_registry_bind(registry, name, &wl_output_interface, 3);
		create_output(state, wl_output);
	} else if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
		state->xdg_output_manager = wl_registry_bind(registry, name,
			&zxdg_output_manager_v1_interface, 1);
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = noop,
};

static const char usage[] =
	"Usage: slurp [options...]\n"
	"\n"
	"  -h           Show help message and quit.\n"
	"  -d           Display dimensions of selection.\n"
	"  -b #rrggbbaa Set background color.\n"
	"  -c #rrggbbaa Set border color.\n"
	"  -s #rrggbbaa Set selection color.\n"
	"  -w n         Set border weight.\n"
	"  -f s         Set output format.\n"
	"  -p           Select a single point.\n";

uint32_t parse_color(const char *color) {
	if (color[0] == '#') {
		++color;
	}

	int len = strlen(color);
	if (len != 6 && len != 8) {
		fprintf(stderr, "Invalid color %s, "
				"defaulting to color 0xFFFFFFFF\n", color);
		return 0xFFFFFFFF;
	}
	uint32_t res = (uint32_t)strtoul(color, NULL, 16);
	if (strlen(color) == 6) {
		res = (res << 8) | 0xFF;
	}
	return res;
}

static void print_formatted_result(const struct slurp_box *result,
		const char *format) {
	for (size_t i = 0; format[i] != '\0'; i++) {
		char c = format[i];
		if (c == '%') {
			char next = format[i + 1];

			i++; // Skip the next character (x, y, w or h)
			switch (next) {
			case 'x':
				printf("%u", result->x);
				continue;
			case 'y':
				printf("%u", result->y);
				continue;
			case 'w':
				printf("%u", result->width);
				continue;
			case 'h':
				printf("%u", result->height);
				continue;
			default:
				// If no case was executed, revert i back - we don't need to
				// skip the next character.
				i--;
			}
		}
		printf("%c", c);
	}
	printf("\n");
}

int main(int argc, char *argv[]) {
	struct slurp_state state = {
		.colors = {
			.background = 0xFFFFFF40,
			.border = 0x000000FF,
			.selection = 0x00000000,
		},
		.border_weight = 2,
		.display_dimensions = false,
	};

	int opt;
	char *format = "%x,%y %wx%h";
	while ((opt = getopt(argc, argv, "hdb:c:s:w:pf:")) != -1) {
		switch (opt) {
		case 'h':
			printf("%s", usage);
			return EXIT_SUCCESS;
		case 'd':
			state.display_dimensions = true;
			break;
		case 'b':
			state.colors.background = parse_color(optarg);
			break;
		case 'c':
			state.colors.border = parse_color(optarg);
			break;
		case 's':
			state.colors.selection = parse_color(optarg);
			break;
		case 'f':
			format = optarg;
			break;
		case 'w': {
			errno = 0;
			char *endptr;
			state.border_weight = strtol(optarg, &endptr, 10);
			if (*endptr || errno) {
				fprintf(stderr, "Error: expected numeric argument for -w\n");
				exit(EXIT_FAILURE);
			}
			break;
		case 'p':
			state.single_point = true;
			break;
		}
		default:
			printf("%s", usage);
			return EXIT_FAILURE;
		}
	}

	wl_list_init(&state.outputs);
	wl_list_init(&state.seats);

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return EXIT_FAILURE;
	}

	state.registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_roundtrip(state.display);

	if (state.compositor == NULL) {
		fprintf(stderr, "compositor doesn't support wl_compositor\n");
		return EXIT_FAILURE;
	}
	if (state.shm == NULL) {
		fprintf(stderr, "compositor doesn't support wl_shm\n");
		return EXIT_FAILURE;
	}
	if (state.layer_shell == NULL) {
		fprintf(stderr, "compositor doesn't support zwlr_layer_shell_v1\n");
		return EXIT_FAILURE;
	}
	if (state.xdg_output_manager == NULL) {
		fprintf(stderr, "compositor doesn't support xdg-output. "
			"Guessing geometry from physical output size.\n");
	}
	if (wl_list_empty(&state.outputs)) {
		fprintf(stderr, "no wl_output\n");
		return EXIT_FAILURE;
	}

	struct slurp_output *output;
	wl_list_for_each(output, &state.outputs, link) {
		output->surface = wl_compositor_create_surface(state.compositor);
		// TODO: wl_surface_add_listener(output->surface, &surface_listener, output);

		output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
			state.layer_shell, output->surface, output->wl_output,
			ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY, "selection");
		zwlr_layer_surface_v1_add_listener(output->layer_surface,
		  &layer_surface_listener, output);

		if (state.xdg_output_manager) {
			output->xdg_output = zxdg_output_manager_v1_get_xdg_output(
				state.xdg_output_manager, output->wl_output);
			zxdg_output_v1_add_listener(output->xdg_output,
				&xdg_output_listener, output);
		} else {
			// guess
			output->logical_geometry = output->geometry;
			output->logical_geometry.width /= output->scale;
			output->logical_geometry.height /= output->scale;
		}

		zwlr_layer_surface_v1_set_anchor(output->layer_surface,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
		zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, true);
		zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
		wl_surface_commit(output->surface);

		output->cursor_theme =
			wl_cursor_theme_load(NULL, 24 * output->scale, state.shm);
		if (output->cursor_theme == NULL) {
			fprintf(stderr, "failed to load cursor theme\n");
			return EXIT_FAILURE;
		}
		struct wl_cursor *cursor =
			wl_cursor_theme_get_cursor(output->cursor_theme, "crosshair");
		if (cursor == NULL) {
			// Fallback
			cursor =
				wl_cursor_theme_get_cursor(output->cursor_theme, "left_ptr");
		}
		if (cursor == NULL) {
			fprintf(stderr, "failed to load cursor\n");
			return EXIT_FAILURE;
		}
		output->cursor_image = cursor->images[0];
	}
	// second roundtrip for xdg-output
	wl_display_roundtrip(state.display);

	struct slurp_seat *seat;
	wl_list_for_each(seat, &state.seats, link) {
		seat->cursor_surface =
			wl_compositor_create_surface(state.compositor);
	}

	state.running = true;
	while (state.running && wl_display_dispatch(state.display) != -1) {
		// This space intentionally left blank
	}

	struct slurp_output *output_tmp;
	wl_list_for_each_safe(output, output_tmp, &state.outputs, link) {
		destroy_output(output);
	}
	struct slurp_seat *seat_tmp;
	wl_list_for_each_safe(seat, seat_tmp, &state.seats, link) {
		destroy_seat(seat);
	}

	// Make sure the compositor has unmapped our surfaces by the time we exit
	wl_display_roundtrip(state.display);

	zwlr_layer_shell_v1_destroy(state.layer_shell);
	if (state.xdg_output_manager != NULL) {
		zxdg_output_manager_v1_destroy(state.xdg_output_manager);
	}
	wl_compositor_destroy(state.compositor);
	wl_shm_destroy(state.shm);
	wl_registry_destroy(state.registry);
	wl_display_disconnect(state.display);

	if (state.result.width == 0 && state.result.height == 0) {
		fprintf(stderr, "selection cancelled\n");
		return EXIT_FAILURE;
	}

	print_formatted_result(&state.result, format);
	return EXIT_SUCCESS;
}
