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

static struct slurp_output *output_from_surface(struct slurp_state *state,
	struct wl_surface *surface);

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct slurp_pointer *pointer = data;
	struct slurp_output *output = output_from_surface(pointer->state, surface);
	if (output == NULL) {
		return;
	}

	pointer->x = wl_fixed_to_int(surface_x);
	pointer->y = wl_fixed_to_int(surface_y);

	// TODO: handle multiple overlapping outputs
	pointer->current_output = output;

	wl_surface_set_buffer_scale(pointer->cursor_surface, output->scale);
	wl_surface_attach(pointer->cursor_surface,
			wl_cursor_image_get_buffer(output->cursor_image), 0, 0);
	wl_pointer_set_cursor(wl_pointer, serial, pointer->cursor_surface,
			output->cursor_image->hotspot_x / output->scale,
			output->cursor_image->hotspot_y / output->scale);
	wl_surface_commit(pointer->cursor_surface);
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	struct slurp_pointer *pointer = data;

	// TODO: handle multiple overlapping outputs
	pointer->current_output = NULL;
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct slurp_pointer *pointer = data;

	pointer->x = wl_fixed_to_int(surface_x);
	pointer->y = wl_fixed_to_int(surface_y);

	if (pointer->button_state == WL_POINTER_BUTTON_STATE_PRESSED &&
			pointer->current_output != NULL) {
		set_output_dirty(pointer->current_output);
	}
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button,
		uint32_t button_state) {
	struct slurp_pointer *pointer = data;
	struct slurp_state *state = pointer->state;

	pointer->button_state = button_state;

	switch (button_state) {
	case WL_POINTER_BUTTON_STATE_PRESSED:
		pointer->pressed_x = pointer->x;
		pointer->pressed_y = pointer->y;
		break;
	case WL_POINTER_BUTTON_STATE_RELEASED:
		pointer_get_box(pointer, &state->result.x, &state->result.y,
			&state->result.width, &state->result.height);
		if (pointer->current_output != NULL) {
			state->result.x += pointer->current_output->geometry.x;
			state->result.y += pointer->current_output->geometry.y;
		}
		state->running = false;
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

static void create_pointer(struct slurp_state *state,
		struct wl_pointer *wl_pointer) {
	struct slurp_pointer *pointer = calloc(1, sizeof(struct slurp_pointer));
	if (pointer == NULL) {
		fprintf(stderr, "allocation failed\n");
		return;
	}
	pointer->state = state;
	pointer->wl_pointer = wl_pointer;
	wl_list_insert(&state->pointers, &pointer->link);

	wl_pointer_add_listener(wl_pointer, &pointer_listener, pointer);
}

static void destroy_pointer(struct slurp_pointer *pointer) {
	wl_list_remove(&pointer->link);
	wl_surface_destroy(pointer->cursor_surface);
	wl_pointer_destroy(pointer->wl_pointer);
	free(pointer);
}

static int min(int a, int b) {
	return (a < b) ? a : b;
}

void pointer_get_box(struct slurp_pointer *pointer, int *x, int *y,
		int *width, int *height) {
	*x = min(pointer->pressed_x, pointer->x);
	*y = min(pointer->pressed_y, pointer->y);
	*width = abs(pointer->x - pointer->pressed_x);
	*height = abs(pointer->y - pointer->pressed_y);
}


static void seat_handle_capabilities(void *data, struct wl_seat *seat,
		uint32_t capabilities) {
	struct slurp_state *state = data;

	if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
		struct wl_pointer *wl_pointer = wl_seat_get_pointer(seat);
		create_pointer(state, wl_pointer);
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
	seat->wl_seat = wl_seat;
	wl_list_insert(&state->seats, &seat->link);
	wl_seat_add_listener(wl_seat, &seat_listener, state);
}

static void destroy_seat(struct slurp_seat *seat) {
	wl_list_remove(&seat->link);
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

static void output_handle_scale(void *data, struct wl_output *wl_output,
		int32_t scale) {
	struct slurp_output *output = data;

	output->scale = scale;
}

static const struct wl_output_listener output_listener = {
	.geometry = output_handle_geometry,
	.mode = noop,
	.done = noop,
	.scale = output_handle_scale,
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
	wl_callback_add_listener(output->frame_callback, &output_frame_listener, output);

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
	wl_callback_add_listener(output->frame_callback, &output_frame_listener, output);
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
	}
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = noop,
};

static const char usage[] =
	"Usage: slurp [options...]\n"
	"\n"
	"  -h         Show help message and quit.\n"
	"  -d         Display dimensions of selection.\n"
	"  -b #rrggbb Set background color.\n"
	"  -c #rrggbb Set border color.\n"
	"  -s #rrggbb Set selection color.\n"
	"  -w n       Set border weight.\n";

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
	while ((opt = getopt(argc, argv, "hdb:c:s:w:")) != -1) {
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
		case 'w': {
			errno = 0;
			char *endptr;
			state.border_weight = strtol(optarg, &endptr, 10);
			if (*endptr || errno) {
				fprintf(stderr, "Error: expected numeric argument for -w\n");
				exit(EXIT_FAILURE);
			}
			break;
		}
		default:
			printf("%s", usage);
			return EXIT_FAILURE;
		}
	}

	wl_list_init(&state.outputs);
	wl_list_init(&state.pointers);
	wl_list_init(&state.seats);

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return EXIT_FAILURE;
	}

	state.registry = wl_display_get_registry(state.display);
	wl_registry_add_listener(state.registry, &registry_listener, &state);
	wl_display_dispatch(state.display);
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

		zwlr_layer_surface_v1_set_anchor(output->layer_surface,
			ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
			ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM);
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

	struct slurp_pointer *pointer;
	wl_list_for_each(pointer, &state.pointers, link) {
		pointer->cursor_surface =
			wl_compositor_create_surface(state.compositor);
	}

	state.running = true;
	while (state.running && wl_display_dispatch(state.display) != -1) {
		// This space intentionally left blank
	}

	struct slurp_pointer *pointer_tmp;
	wl_list_for_each_safe(pointer, pointer_tmp, &state.pointers, link) {
		destroy_pointer(pointer);
	}
	struct slurp_output *output_tmp;
	wl_list_for_each_safe(output, output_tmp, &state.outputs, link) {
		destroy_output(output);
	}
	struct slurp_seat *seat, *seat_tmp;
	wl_list_for_each_safe(seat, seat_tmp, &state.seats, link) {
		destroy_seat(seat);
	}

	// Make sure the compositor has unmapped our surfaces by the time we exit
	wl_display_roundtrip(state.display);

	zwlr_layer_shell_v1_destroy(state.layer_shell);
	wl_compositor_destroy(state.compositor);
	wl_shm_destroy(state.shm);
	wl_registry_destroy(state.registry);
	wl_display_disconnect(state.display);

	if (state.result.width == 0 && state.result.height == 0) {
		fprintf(stderr, "selection cancelled\n");
		return EXIT_FAILURE;
	}

	printf("%d,%d %dx%d\n", state.result.x, state.result.y,
		state.result.width, state.result.height);
	return EXIT_SUCCESS;
}
