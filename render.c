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

void render(struct slurp_state *state, struct pool_buffer *buffer) {
	cairo_t *cairo = buffer->cairo;

	// Clear
	cairo_save(cairo);
	cairo_set_source_rgba(cairo, 0, 0, 0, 0);
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	cairo_paint(cairo);
	cairo_restore(cairo);

	struct slurp_pointer *pointer;
	wl_list_for_each(pointer, &state->pointers, link) {
		if (pointer->button_state != WL_POINTER_BUTTON_STATE_PRESSED) {
			continue;
		}

		int x, y, width, height;
		pointer_get_box(pointer, &x, &y, &width, &height);

		// Draw border
		int border_size = 2;
		set_source_u32(cairo, 0x000000FF);
		cairo_set_line_width(cairo, border_size);
		cairo_rectangle(cairo, x, y, width, height);
		cairo_stroke(cairo);
	}
}
