#ifndef _SLURP_H
#define _SLURP_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

#include "box.h"
#include "cursor-shape-v1-client-protocol.h"
#include "pool-buffer.h"
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h"

#define TOUCH_ID_EMPTY -1

struct slurp_selection {
  struct slurp_output *current_output;
  int32_t x, y;
  int32_t anchor_x, anchor_y;
  struct slurp_box selection;
  bool has_selection;
};

struct slurp_state {
	bool running;
	bool edit_anchor;

	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_shm *shm;
	struct wl_compositor *compositor;
	struct zwlr_layer_shell_v1 *layer_shell;
	struct zxdg_output_manager_v1 *xdg_output_manager;
	struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
	struct wl_list outputs; // slurp_output::link
	struct wl_list seats; // slurp_seat::link

	struct xkb_context *xkb_context;

	struct {
		uint32_t background;
		uint32_t border;
		uint32_t selection;
		uint32_t choice;
	} colors;

	const char *font_family;

	uint32_t border_weight;
	bool display_dimensions;
	bool single_point;
	bool restrict_selection;
	bool confirm_selection;
  bool crosshairs;
  bool resizing_selection;
	struct wl_list boxes; // slurp_box::link
	bool fixed_aspect_ratio;
	double aspect_ratio;  // h / w

	struct slurp_box result;
};

struct slurp_output {
  struct wl_output *wl_output;
  struct slurp_state *state;
  struct wl_list link; // slurp_state::outputs

  struct slurp_box geometry;
  struct slurp_box logical_geometry;
  int32_t scale;

  struct wl_surface *surface;
  struct zwlr_layer_surface_v1 *layer_surface;

  struct zxdg_output_v1 *xdg_output;

  struct wl_callback *frame_callback;
  bool configured;
  bool dirty;
  int32_t width, height;
  struct pool_buffer buffers[2];
  struct pool_buffer *current_buffer;

  struct wl_cursor_theme *cursor_theme;
  struct wl_cursor_image *cursor_image;
};

struct slurp_seat {
  struct wl_surface *cursor_surface;
  struct slurp_state *state;
  struct wl_seat *wl_seat;
  struct wl_list link; // slurp_state::seats

  // keyboard:
  struct wl_keyboard *wl_keyboard;

  // selection (pointer/touch):

  struct slurp_selection pointer_selection;
  struct slurp_selection touch_selection;

  // pointer:
  struct wl_pointer *wl_pointer;
  enum wl_pointer_button_state button_state;

  // keymap:
  struct xkb_keymap *xkb_keymap;
  struct xkb_state *xkb_state;

  // touch:
  struct wl_touch *wl_touch;
  int32_t touch_id;
};

bool box_intersect(const struct slurp_box *a, const struct slurp_box *b);

static inline struct slurp_selection *
slurp_seat_current_selection(struct slurp_seat *seat) {
  return seat->touch_selection.has_selection ? &seat->touch_selection
                                             : &seat->pointer_selection;
}
#endif
