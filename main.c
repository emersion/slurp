#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>

#include "slurp.h"
#include "render.h"
#include "lock.h"

#define BG_COLOR 0xFFFFFF40
#define BORDER_COLOR 0x000000FF
#define SELECTION_COLOR 0x00000000
#define FONT_FAMILY "sans-serif"

static void noop() {
	// This space intentionally left blank
}

static void set_output_dirty(struct slurp_output *output);

static int max(int a, int b) {
	return (a > b) ? a : b;
}

static int min(int a, int b) {
	return (a < b) ? a : b;
}

static struct slurp_output *output_from_surface(struct slurp_state *state,
	struct wl_surface *surface);

static void move_seat(struct slurp_seat *seat, wl_fixed_t surface_x,
		wl_fixed_t surface_y,
		struct slurp_selection *current_selection) {
	int x = wl_fixed_to_int(surface_x) +
		current_selection->current_output->logical_geometry.x;
	int y = wl_fixed_to_int(surface_y) + current_selection->current_output->logical_geometry.y;

	if (seat->state->edit_anchor) {
		current_selection->anchor_x += x - current_selection->x;
		current_selection->anchor_y += y - current_selection->y;
	}

	current_selection->x = x;
	current_selection->y = y;
}

static void seat_update_selection(struct slurp_seat *seat) {
	seat->pointer_selection.has_selection = false;

	// find smallest box intersecting the cursor
	struct slurp_box *box;
	wl_list_for_each(box, &seat->state->boxes, link) {
		if (in_box(box, seat->pointer_selection.x,
			   seat->pointer_selection.y)) {
			if (seat->pointer_selection.has_selection &&
				box_size(
					&seat->pointer_selection.selection) <
					box_size(box)) {
				continue;
			}
			seat->pointer_selection.selection = *box;
			seat->pointer_selection.has_selection = true;
		}
	}
}

static void seat_set_outputs_dirty(struct slurp_seat *seat) {
	struct slurp_state *state = seat->state;
	struct slurp_output *output;
	wl_list_for_each(output, &seat->state->outputs, link) {
		struct slurp_box *geometry = &output->logical_geometry;
		if (box_intersect(geometry, &seat->pointer_selection.selection) ||
				box_intersect(geometry, &seat->touch_selection.selection) ||
				(state->crosshairs && in_box(geometry, seat->pointer_selection.x, seat->pointer_selection.y))) {
			set_output_dirty(output);
		}
	}
}

static void handle_active_selection_motion(struct slurp_seat *seat, struct slurp_selection *current_selection) {
	if(seat->state->restrict_selection){
		return;
	}

	seat->state->resizing_selection = true;

	int32_t anchor_x = current_selection->anchor_x;
	int32_t anchor_y = current_selection->anchor_y;
	int32_t dist_x = current_selection->x - anchor_x;
	int32_t dist_y = current_selection->y - anchor_y;

	current_selection->has_selection = true;
	// selection includes the seat and anchor positions
	int32_t width = abs(dist_x) + 1;
	int32_t height = abs(dist_y) + 1;
	if (seat->state->aspect_ratio) {
		width = max(width, height / seat->state->aspect_ratio);
		height = max(height, width * seat->state->aspect_ratio);
	}
	current_selection->selection.x = dist_x > 0 ? anchor_x : anchor_x - (width - 1);
	current_selection->selection.y = dist_y > 0 ? anchor_y : anchor_y - (height - 1);
	current_selection->selection.width = width;
	current_selection->selection.height = height;
}

static void pointer_handle_enter(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface,
		wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct slurp_seat *seat = data;
	struct slurp_output *output = output_from_surface(seat->state, surface);
	if (output == NULL) {
		return;
	}

	// the places the cursor moved away from are also dirty
	if (seat->pointer_selection.has_selection || seat->state->crosshairs) {
		seat_set_outputs_dirty(seat);
	}

	// TODO: handle multiple overlapping outputs
	seat->pointer_selection.current_output = output;

	move_seat(seat, surface_x, surface_y, &seat->pointer_selection);

	switch (seat->button_state) {
	case WL_POINTER_BUTTON_STATE_RELEASED:
		seat_update_selection(seat);
		break;
	case WL_POINTER_BUTTON_STATE_PRESSED:
		handle_active_selection_motion(seat, &seat->pointer_selection);
		break;
	}

	seat_set_outputs_dirty(seat);

	if (output->state->cursor_shape_manager) {
		struct wp_cursor_shape_device_v1 *device =
			wp_cursor_shape_manager_v1_get_pointer(
				output->state->cursor_shape_manager, wl_pointer);
		wp_cursor_shape_device_v1_set_shape(device, serial,
			WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR);
		wp_cursor_shape_device_v1_destroy(device);
	} else {
		wl_surface_set_buffer_scale(seat->cursor_surface, output->scale);
		wl_surface_attach(seat->cursor_surface,
			wl_cursor_image_get_buffer(output->cursor_image), 0, 0);
		wl_pointer_set_cursor(wl_pointer, serial, seat->cursor_surface,
			output->cursor_image->hotspot_x / output->scale,
			output->cursor_image->hotspot_y / output->scale);
		wl_surface_commit(seat->cursor_surface);
	}
}

static void pointer_handle_leave(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, struct wl_surface *surface) {
	struct slurp_seat *seat = data;

	// TODO: handle multiple overlapping outputs
	seat->pointer_selection.current_output = NULL;
}

static void pointer_handle_motion(void *data, struct wl_pointer *wl_pointer,
		uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
	struct slurp_seat *seat = data;
	struct slurp_state *state = seat->state;

	// the places the cursor moved away from are also dirty
	if (seat->pointer_selection.has_selection || state->crosshairs) {
		seat_set_outputs_dirty(seat);
	}

	move_seat(seat, surface_x, surface_y, &seat->pointer_selection);

	switch (seat->button_state) {
	case WL_POINTER_BUTTON_STATE_RELEASED:
		seat_update_selection(seat);
		break;
	case WL_POINTER_BUTTON_STATE_PRESSED:
		handle_active_selection_motion(seat, &seat->pointer_selection);
		break;
	}

	if (seat->pointer_selection.has_selection || state->crosshairs) {
		seat_set_outputs_dirty(seat);
	}
}

static void handle_selection_start(struct slurp_seat *seat,
				   struct slurp_selection *current_selection) {
	struct slurp_state *state = seat->state;

	if (state->single_point) {
		state->result.x = current_selection->x;
		state->result.y = current_selection->y;
		state->result.width = state->result.height = 1;
		state->running = false;
	} else if (state->restrict_selection) {
		if (current_selection->has_selection) {
			state->result = current_selection->selection;
			state->running = false;
		}
	} else {
		current_selection->anchor_x = current_selection->x;
		current_selection->anchor_y = current_selection->y;
	}
}

static void handle_selection_end(struct slurp_seat *seat,
				 struct slurp_selection *current_selection) {
	struct slurp_state *state = seat->state;
	if (state->single_point || state->restrict_selection) {
		return;
	}
	if (current_selection->has_selection) {
		state->result = current_selection->selection;
	} else {
		state->result.x = current_selection->x;
		state->result.y = current_selection->y;
		state->result.width = state->result.height = 1;
	}
	state->resizing_selection = false;
  if (!state->confirm_selection) {
		state->running = false;
	}
}

static void handle_selection_cancelled(struct slurp_seat *seat) {
	struct slurp_state *state = seat->state;
	seat->pointer_selection.has_selection = false;
	seat->touch_selection.has_selection = false;
	state->edit_anchor = false;
	state->running = false;
}

static void pointer_handle_button(void *data, struct wl_pointer *wl_pointer,
		uint32_t serial, uint32_t time, uint32_t button,
		uint32_t button_state) {
	struct slurp_seat *seat = data;
	if (seat->touch_selection.has_selection) {
		return;
	}

	seat->button_state = button_state;
	switch (button) {
	case BTN_LEFT:
		switch (button_state) {
		case WL_POINTER_BUTTON_STATE_PRESSED:
			handle_selection_start(seat, &seat->pointer_selection);
			break;
		case WL_POINTER_BUTTON_STATE_RELEASED:
			handle_selection_end(seat, &seat->pointer_selection);
			break;
		}
		break;
	default: //other mouse buttons cancel the selection
		handle_selection_cancelled(seat);
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

static void keyboard_handle_keymap(void *data, struct wl_keyboard *wl_keyboard,
		const uint32_t format, const int32_t fd, const uint32_t size) {
	struct slurp_seat *seat = data;
	switch (format) {
	case WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP:
		seat->xkb_keymap = xkb_keymap_new_from_names(seat->state->xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
		break;
	case WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1:;
		void *buffer;
		if ((buffer = mmap(NULL, size - 1, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
			fprintf(stderr, "mmap failed\n");
			exit(EXIT_FAILURE);
		}
		seat->xkb_keymap =
			xkb_keymap_new_from_buffer(seat->state->xkb_context,
					buffer, size - 1,
					XKB_KEYMAP_FORMAT_TEXT_V1,
					XKB_KEYMAP_COMPILE_NO_FLAGS);
		munmap(buffer, size - 1);
		close(fd);
		break;
	}
	seat->xkb_state = xkb_state_new(seat->xkb_keymap);
}

// Recompute the selection if the aspect ratio changed.
static void recompute_selection(struct slurp_seat *seat) {
	struct slurp_selection *current = slurp_seat_current_selection(seat);
	if (current->has_selection) {
		handle_active_selection_motion(seat, slurp_seat_current_selection(seat));
		seat_set_outputs_dirty(seat);
	}
}

static void keyboard_handle_key(void *data, struct wl_keyboard *wl_keyboard,
		const uint32_t serial, const uint32_t time, const uint32_t key,
		const uint32_t key_state) {
	struct slurp_seat *seat = data;
	struct slurp_state *state = seat->state;
	const xkb_keysym_t keysym = xkb_state_key_get_one_sym(seat->xkb_state, key + 8);

	switch (key_state) {
	case WL_KEYBOARD_KEY_STATE_PRESSED:
		switch (keysym) {
		case XKB_KEY_Escape:
			handle_selection_cancelled(seat);
			break;

		case XKB_KEY_space:
			if (!seat->pointer_selection.has_selection &&
					!seat->touch_selection.has_selection) {
				break;
			}
			state->edit_anchor = true;
			break;
		
		case XKB_KEY_Shift_L:
		case XKB_KEY_Shift_R:
			if (!state->fixed_aspect_ratio) {
				state->aspect_ratio = 1;
				if (state->resizing_selection) {
					recompute_selection(seat);
				}
			}
			break;

		case XKB_KEY_Return:
			state->running = false;
			break;
		}
		break;

	case WL_KEYBOARD_KEY_STATE_RELEASED:
		switch (keysym) {
		case XKB_KEY_space:
			state->edit_anchor = false;
			break;
		case XKB_KEY_Shift_L:
		case XKB_KEY_Shift_R:
			if (!state->fixed_aspect_ratio) {
				state->aspect_ratio = 0;
				if (state->resizing_selection) {
          recompute_selection(seat);
        }
			}
			break;
		}
	}

}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *wl_keyboard,
		const uint32_t serial, const uint32_t mods_depressed,
		const uint32_t mods_latched, const uint32_t mods_locked,
		const uint32_t group) {
	struct slurp_seat *seat = data;
	xkb_state_update_mask(seat->xkb_state, mods_depressed, mods_latched,
			mods_locked, 0, 0, group);
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = noop,
	.leave = noop,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
};

static void touch_handle_down(void *data, struct wl_touch *touch,
		uint32_t serial, uint32_t time,
		struct wl_surface *surface, int32_t id,
		wl_fixed_t x, wl_fixed_t y) {
	struct slurp_seat *seat = data;
	if (seat->pointer_selection.has_selection) {
		return;
	}
	if (seat->touch_id == TOUCH_ID_EMPTY) {
		seat->touch_id = id;
		seat->touch_selection.current_output =
			output_from_surface(seat->state, surface);
		move_seat(seat, x, y, &seat->touch_selection);
		handle_selection_start(seat, &seat->touch_selection);
	}
}

static void touch_clear_state(struct slurp_seat *seat) {
	seat->touch_id = TOUCH_ID_EMPTY;
	seat->touch_selection.current_output = NULL;
}

static void touch_handle_up(void *data, struct wl_touch *touch, uint32_t serial,
		uint32_t time, int32_t id) {
	struct slurp_seat *seat = data;
	handle_selection_end(seat, &seat->touch_selection);
	touch_clear_state(seat);
}

static void touch_handle_motion(void *data, struct wl_touch *touch,
		uint32_t time, int32_t id, wl_fixed_t x,
		wl_fixed_t y) {
	struct slurp_seat *seat = data;
	if (seat->touch_id == id) {
		move_seat(seat, x, y, &seat->touch_selection);
		handle_active_selection_motion(seat, &seat->touch_selection);
		seat_set_outputs_dirty(seat);
	}
}

static void touch_handle_cancel(void *data, struct wl_touch *touch) {
	struct slurp_seat *seat = data;
	touch_clear_state(seat);
}

static const struct wl_touch_listener touch_listener = {
	.down = touch_handle_down,
	.up = touch_handle_up,
	.frame = noop,
	.motion = touch_handle_motion,
	.orientation = noop,
	.shape = noop,
	.cancel = touch_handle_cancel,
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
	if (capabilities & WL_SEAT_CAPABILITY_TOUCH) {
		seat->wl_touch = wl_seat_get_touch(wl_seat);
		wl_touch_add_listener(seat->wl_touch, &touch_listener, seat);
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
	seat->touch_id = TOUCH_ID_EMPTY;
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
	xkb_state_unref(seat->xkb_state);
	xkb_keymap_unref(seat->xkb_keymap);
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

static void xdg_output_handle_name(void *data, struct zxdg_output_v1 *xdg_output, const char *name) {
	struct slurp_output *output = data;
	output->logical_geometry.label = strdup(name);
}

static const struct zxdg_output_v1_listener xdg_output_listener = {
	.logical_position = xdg_output_handle_logical_position,
	.logical_size = xdg_output_handle_logical_size,
	.done = noop,
	.name = xdg_output_handle_name,
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
	if (output->cursor_theme) {
		wl_cursor_theme_destroy(output->cursor_theme);
	}
	zwlr_layer_surface_v1_destroy(output->layer_surface);
	if (output->xdg_output) {
		zxdg_output_v1_destroy(output->xdg_output);
	}
	wl_surface_destroy(output->surface);
	if (output->frame_callback) {
		wl_callback_destroy(output->frame_callback);
	}
	wl_output_destroy(output->wl_output);
	free(output->logical_geometry.label);
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
	output->current_buffer->busy = true;

	cairo_identity_matrix(output->current_buffer->cairo);
	cairo_scale(output->current_buffer->cairo, output->scale, output->scale);
	cairo_translate(output->current_buffer->cairo, -output->logical_geometry.x, -output->logical_geometry.y);

	render(output);

	// Schedule a frame in case the output becomes dirty again
	if (output->frame_callback) {
		wl_callback_destroy(output->frame_callback);
	}
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
			&zxdg_output_manager_v1_interface, 2);
	} else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0) {
		state->cursor_shape_manager = wl_registry_bind(registry, name,
			&wp_cursor_shape_manager_v1_interface, 1);
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
	"  -B #rrggbbaa Set option box color.\n"
	"  -F s         Set the font family for the dimensions.\n"
	"  -w n         Set border weight.\n"
	"  -f s         Set output format.\n"
	"  -o           Select a display output.\n"
	"  -p           Select a single point.\n"
	"  -r           Restrict selection to predefined boxes.\n"
	"  -C           Confirm selection with enter key.\n"
	"  -a w:h       Force aspect ratio.\n"
    "  -x           Display crosshairs across active display output.\n";

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

static struct slurp_output *output_from_box(const struct slurp_box *box, struct wl_list *outputs) {
	struct slurp_output *output;
	wl_list_for_each(output, outputs, link) {
		struct slurp_box *geometry = &output->logical_geometry;
		// For now just use the top-left corner
		if (in_box(geometry, box->x, box->y)) {
			return output;
		}
	}
	return NULL;
}

static void print_output_name(FILE *stream, const struct slurp_box *result, struct wl_list *outputs) {
	struct slurp_output *output = output_from_box(result, outputs);
	if (output) {
		struct slurp_box *geometry = &output->logical_geometry;
		if (geometry->label) {
			fprintf(stream, "%s", geometry->label);
			return;
		}
	}
	fprintf(stream, "<unknown>");
}

static void print_formatted_result(FILE *stream, struct slurp_state *state , const char *format) {
	struct slurp_output *output = output_from_box(&state->result, &state->outputs);
	for (size_t i = 0; format[i] != '\0'; i++) {
		char c = format[i];
		if (c == '%') {
			char next = format[i + 1];

			i++; // Skip the next character (x, y, w or h)
			switch (next) {
			case 'x':
				fprintf(stream, "%d", state->result.x);
				continue;
			case 'y':
				fprintf(stream, "%d", state->result.y);
				continue;
			case 'w':
				fprintf(stream, "%d", state->result.width);
				continue;
			case 'h':
				fprintf(stream, "%d", state->result.height);
				continue;
			case 'X':
				assert(output);
				fprintf(stream, "%d", state->result.x - output->logical_geometry.x);
				continue;
			case 'Y':
				assert(output);
				fprintf(stream, "%d", state->result.y - output->logical_geometry.y);
				continue;
			case 'W':
				assert(output);
				fprintf(stream, "%d", min(state->result.width, output->logical_geometry.x + output->logical_geometry.width - state->result.x));
				continue;
			case 'H':
				assert(output);
				fprintf(stream, "%d", min(state->result.height, output->logical_geometry.y + output->logical_geometry.height - state->result.y));
				continue;
			case 'l':
				if (state->result.label) {
					fprintf(stream, "%s", state->result.label);
				}
				continue;
			case 'o':
				print_output_name(stream, &state->result, &state->outputs);
				continue;
			default:
				// If no case was executed, revert i back - we don't need to
				// skip the next character.
				i--;
			}
		}
		fprintf(stream, "%c", c);
	}
}

static void add_choice_box(struct slurp_state *state,
		const struct slurp_box *box) {
	struct slurp_box *b = calloc(1, sizeof(struct slurp_box));
	if (b == NULL) {
		fprintf(stderr, "allocation failed\n");
		return;
	}
	*b = *box;
	// copy label, so that this has ownership of its label
	if (box->label) {
		b->label = strdup(box->label);
	}
	wl_list_insert(state->boxes.prev, &b->link);
}

static bool create_cursors(struct slurp_state *state) {
	const char *cursor_theme = getenv("XCURSOR_THEME");
	const char *cursor_size_str = getenv("XCURSOR_SIZE");
	int cursor_size = 24;
	if (cursor_size_str != NULL) {
		char *end;
		errno = 0;
		cursor_size = strtol(cursor_size_str, &end, 10);
		if (errno != 0 || cursor_size_str[0] == '\0' || end[0] != '\0') {
			fprintf(stderr, "invalid XCURSOR_SIZE value\n");
			return false;
		}
	}

	struct slurp_output *output;
	wl_list_for_each(output, &state->outputs, link) {
		output->cursor_theme = wl_cursor_theme_load(cursor_theme,
			cursor_size * output->scale, state->shm);
		if (output->cursor_theme == NULL) {
			fprintf(stderr, "failed to load cursor theme\n");
			return false;
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
			return false;
		}
		output->cursor_image = cursor->images[0];
	}

	return true;
}

int main(int argc, char *argv[]) {
	int status = EXIT_SUCCESS;

	struct slurp_state state = {
		.colors = {
			.background = BG_COLOR,
			.border = BORDER_COLOR,
			.selection = SELECTION_COLOR,
			.choice = BG_COLOR,
		},
		.border_weight = 2,
		.display_dimensions = false,
		.restrict_selection = false,
		.resizing_selection = false,
		.fixed_aspect_ratio = false,
		.confirm_selection = false,
		.aspect_ratio = 0,
		.font_family = FONT_FAMILY
	};

	int opt;
	char *format = "%x,%y %wx%h\n";
	bool output_boxes = false;
	int w, h;
	while ((opt = getopt(argc, argv, "hdb:c:s:B:w:prCoa:f:F:x")) != -1) {
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
		case 'B':
			state.colors.choice = parse_color(optarg);
			break;
		case 'f':
			format = optarg;
			break;
		case 'F':
			state.font_family = optarg;
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
		case 'p':
			state.single_point = true;
			break;
		case 'o':
			output_boxes = true;
			break;
		case 'r':
			state.restrict_selection = true;
			break;
		case 'C':
			state.confirm_selection = true;
			break;
		case 'a':
			if (sscanf(optarg, "%d:%d", &w, &h) != 2) {
				fprintf(stderr, "invalid aspect ratio\n");
				return EXIT_FAILURE;
			}
			if (w <= 0 || h <= 0) {
				fprintf(stderr, "width and height of aspect ratio must be greater than zero\n");
				return EXIT_FAILURE;
			}
			state.fixed_aspect_ratio = true;
			state.aspect_ratio = (double) h / w;
			break;
		case 'x':
			state.crosshairs = true;
			break;
		default:
			printf("%s", usage);
			return EXIT_FAILURE;
		}
	}

	if (state.single_point && state.restrict_selection) {
		fprintf(stderr, "-p and -r cannot be used together\n");
		return EXIT_FAILURE;
	}

	if (state.single_point && state.confirm_selection) {
		fprintf(stderr, "-p and -C cannot be used together\n");
    	return EXIT_FAILURE;
	}
  
	if (!acquire_lock()) {
		// acquire_lock prints an appropriate error message itself
		return EXIT_FAILURE;
	}

	wl_list_init(&state.boxes);
	if (!isatty(STDIN_FILENO) && !state.single_point) {
		char *line = NULL;
		size_t line_size = 0;
		while (getline(&line, &line_size, stdin) >= 0) {
			struct slurp_box in_box = {0};
			if (sscanf(line, "%d,%d %dx%d %m[^\n]", &in_box.x, &in_box.y,
					&in_box.width, &in_box.height, &in_box.label) < 4) {
				fprintf(stderr, "invalid box format: %s\n", line);
				return EXIT_FAILURE;
			}
			add_choice_box(&state, &in_box);
			free(in_box.label);
		}
		free(line);
	}
	wl_list_init(&state.outputs);
	wl_list_init(&state.seats);

	state.display = wl_display_connect(NULL);
	if (state.display == NULL) {
		fprintf(stderr, "failed to create display\n");
		return EXIT_FAILURE;
	}

	if ((state.xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS)) == NULL) {
		fprintf(stderr, "xkb_context_new failed\n");
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
	}
	// second roundtrip for xdg-output
	wl_display_roundtrip(state.display);

	if (!state.cursor_shape_manager && !create_cursors(&state)) {
		return EXIT_FAILURE;
	}

	if (output_boxes) {
		struct slurp_output *box_output;
		wl_list_for_each(box_output, &state.outputs, link) {
			add_choice_box(&state, &box_output->logical_geometry);
		}
	}

	struct slurp_seat *seat;
	wl_list_for_each(seat, &state.seats, link) {
		seat->cursor_surface =
			wl_compositor_create_surface(state.compositor);
	}

	state.running = true;
	while (state.running && wl_display_dispatch(state.display) != -1) {
		// This space intentionally left blank
	}

	char *result_str = 0;
	size_t length;
	if (state.result.width == 0 && state.result.height == 0) {
		fprintf(stderr, "selection cancelled\n");
		status = EXIT_FAILURE;
	} else {
		FILE *stream = open_memstream(&result_str, &length);
		print_formatted_result(stream, &state, format);
		fclose(stream);
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
	if (state.cursor_shape_manager != NULL) {
		wp_cursor_shape_manager_v1_destroy(state.cursor_shape_manager);
	}
	wl_compositor_destroy(state.compositor);
	wl_shm_destroy(state.shm);
	wl_registry_destroy(state.registry);
	xkb_context_unref(state.xkb_context);
	wl_display_disconnect(state.display);

	struct slurp_box *box, *box_tmp;
	wl_list_for_each_safe(box, box_tmp, &state.boxes, link) {
		wl_list_remove(&box->link);
		free(box->label);
		free(box);
	}

	if (result_str) {
		printf("%s", result_str);
		free(result_str);
	}

	return status;
}
