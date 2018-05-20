#ifndef _SLURG_H
#define _SLURG_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

struct slurg_state {
	bool running;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct wl_compositor *compositor;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct wl_list outputs; // slurg_output::link
	struct wl_list pointers; // slurg_pointer::link

	struct {
		int32_t x, y;
		int32_t width, height;
	} result;
};

struct slurg_output {
	struct wl_output *wl_output;
	struct slurg_state *state;
	struct wl_list link; // slurg_state::outputs

	int32_t x, y;

	struct wl_surface *surface;
	struct zwlr_layer_surface_v1 *layer_surface;

	bool configured;
	int32_t width, height;
	struct pool_buffer buffers[2];
	struct pool_buffer *current_buffer;
};

struct slurg_pointer {
	struct slurg_state *state;
	struct wl_pointer *wl_pointer;
	struct wl_list link; // slurg_state::pointers

	int32_t x, y;
	int32_t pressed_x, pressed_y;
	enum wl_pointer_button_state button_state;
};

void pointer_get_box(struct slurg_pointer *pointer, int *x, int *y,
	int *width, int *height);

#endif
