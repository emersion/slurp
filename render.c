#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>

#include "pool-buffer.h"
#include "render.h"
#include "slurp.h"

static void set_source_u32(cairo_t *cairo, uint32_t color) {
	cairo_set_source_rgba(cairo,
		(color >> (3*8) & 0xFF) / 255.0,
		(color >> (2*8) & 0xFF) / 255.0,
		(color >> (1*8) & 0xFF) / 255.0,
		(color >> (0*8) & 0xFF) / 255.0);
}

void render(struct slurp_output *output) {
	struct slurp_state *state = output->state;
	struct pool_buffer *buffer = output->current_buffer;
	cairo_t *cairo = buffer->cairo;
	int32_t scale = output->scale;

	// Clear
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	set_source_u32(cairo, state->colors.background);
	cairo_paint(cairo);

	struct slurp_seat *seat;
	wl_list_for_each(seat, &state->seats, link) {
		if (!seat->wl_pointer) continue;
		if (seat->button_state != WL_POINTER_BUTTON_STATE_PRESSED) {
			continue;
		}

		struct slurp_box b;
		seat_get_box(seat, &b);
		if (!box_intersect(&output->logical_geometry, &b)) {
      continue;
    }
		b.x -= output->logical_geometry.x;
		b.y -= output->logical_geometry.y;

		// Draw border
		set_source_u32(cairo, state->colors.selection);
		cairo_rectangle(cairo, b.x * scale, b.y * scale,
			b.width * scale, b.height * scale);
		cairo_fill(cairo);

		set_source_u32(cairo, state->colors.border);
		cairo_set_line_width(cairo, state->border_weight * scale);
		cairo_rectangle(cairo, b.x * scale, b.y * scale,
			b.width * scale, b.height * scale);
		cairo_stroke(cairo);

		if (state->display_dimensions) {
			cairo_select_font_face(cairo, "Sans", CAIRO_FONT_SLANT_NORMAL,
				CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cairo, 14 * scale);
			// buffer of 12 can hold selections up to 99999x99999
			char dimensions[12];
			snprintf(dimensions, sizeof(dimensions), "%ix%i", b.width, b.height);
			cairo_move_to(cairo, (b.x + b.width + 10) * scale, (b.y + b.height + 20) * scale);
			cairo_show_text(cairo, dimensions);
		}
	}
}
