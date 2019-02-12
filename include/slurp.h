#ifndef _SLURP_H
#define _SLURP_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct slurp_box {
	int32_t x, y;
	int32_t width, height;
};

struct slurp_state {
	bool running;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct wl_compositor *compositor;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_list outputs; // slurp_output::link
	struct wl_list pointers; // slurp_pointer::link
	struct wl_list seats; // slurp_seat::link

	struct {
		uint32_t background;
		uint32_t border;
		uint32_t selection;
	} colors;

	uint32_t border_weight;
	bool display_dimensions;

	struct slurp_box result;
};

struct slurp_output {
	struct wl_output *wl_output;
	struct slurp_state *state;
	struct wl_list link; // slurp_state::outputs

	struct slurp_box geometry;
	int32_t scale;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;

	struct wl_callback *frame_callback;
	bool configured;
	bool dirty;
	int32_t width, height;
	struct pool_buffer buffers[2];
	struct pool_buffer *current_buffer;

	struct wl_cursor_theme *cursor_theme;
	struct wl_cursor_image *cursor_image;
};

struct slurp_pointer {
	struct slurp_state *state;
	struct wl_pointer *wl_pointer;
	struct wl_list link; // slurp_state::pointers

	int32_t x, y;
	int32_t pressed_x, pressed_y;
	enum wl_pointer_button_state button_state;
	struct slurp_output *current_output;

	struct wl_surface *cursor_surface;
};

struct slurp_seat {
	struct wl_seat *wl_seat;
	struct wl_list link; // slurp_state::seats
};

void pointer_get_box(struct slurp_pointer *pointer, int *x, int *y,
	int *width, int *height);

#endif
