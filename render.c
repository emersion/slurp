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

	uint32_t border_color = 0x000000FF;
	int border_size = 2;

	// Clear
	cairo_save(cairo);
	cairo_set_source_rgba(cairo, 0, 0, 0, 0);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_restore(cairo);

	struct slurp_pointer *pointer;
	wl_list_for_each(pointer, &state->pointers, link) {
		if (pointer->button_state != WL_POINTER_BUTTON_STATE_PRESSED ||
				pointer->current_output != output) {
			continue;
		}

		int x, y, width, height;
		pointer_get_box(pointer, &x, &y, &width, &height);

		// Draw border
		set_source_u32(cairo, border_color);
		cairo_set_line_width(cairo, border_size * scale);
		cairo_rectangle(cairo, x * scale, y * scale,
			width * scale, height * scale);
		cairo_stroke(cairo);
	}
}
