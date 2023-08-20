#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>

#include "pool-buffer.h"
#include "render.h"
#include "slurp.h"

#define SLURP_PI 3.141592653589793238462643383279

static void set_source_u32(cairo_t *cairo, uint32_t color) {
	cairo_set_source_rgba(cairo, (color >> (3 * 8) & 0xFF) / 255.0,
		(color >> (2 * 8) & 0xFF) / 255.0,
		(color >> (1 * 8) & 0xFF) / 255.0,
		(color >> (0 * 8) & 0xFF) / 255.0);
}

static void draw_rect(cairo_t *cairo, struct slurp_box *box, uint32_t color) {
	set_source_u32(cairo, color);
	cairo_rectangle(cairo, box->x, box->y,
			box->width, box->height);
}

static void draw_grabbers(cairo_t* cairo, struct slurp_box *box, uint32_t radius, uint32_t fill_color, uint32_t border_color) {
	set_source_u32(cairo, fill_color);

	// Top Left
	cairo_arc(cairo, box->x, box->y, radius, 0.0, 2.0*SLURP_PI);

	// Top Right
	cairo_arc(cairo, box->x + box->width, box->y, radius, 0.0, 2.0*SLURP_PI);
	cairo_fill(cairo);

	// Bottom Left
	cairo_arc(cairo, box->x, box->y + box->height, radius, 0.0, 2.0*SLURP_PI);

	// Bottom Right
	cairo_arc(cairo, box->x + box->width, box->y + box->height, radius, 0.0, 2.0*SLURP_PI);
	cairo_fill(cairo);

	// Top
	cairo_arc(cairo, box->x + box->width / 2, box->y, radius, 0.0, 2.0*SLURP_PI);

	// Bottom
	cairo_arc(cairo, box->x + box->width / 2, box->y + box->height, radius, 0.0, 2.0*SLURP_PI);
	cairo_fill(cairo);

	// Left
	cairo_arc(cairo, box->x, box->y + box->height / 2, radius, 0.0, 2.0*SLURP_PI);

	// Right
	cairo_arc(cairo, box->x + box->width, box->y + box->height / 2, radius, 0.0, 2.0*SLURP_PI);
	cairo_fill(cairo);

	set_source_u32(cairo, border_color);

	// Top Left
	cairo_arc(cairo, box->x, box->y, radius, 0.0, 2.0*SLURP_PI);
	cairo_stroke(cairo);

	// Top Right
	cairo_arc(cairo, box->x + box->width, box->y, radius, 0.0, 2.0*SLURP_PI);
	cairo_stroke(cairo);

	// Bottom Left
	cairo_arc(cairo, box->x, box->y + box->height, radius, 0.0, 2.0*SLURP_PI);
	cairo_stroke(cairo);

	// Bottom Right
	cairo_arc(cairo, box->x + box->width, box->y + box->height, radius, 0.0, 2.0*SLURP_PI);
	cairo_stroke(cairo);

	// Top
	cairo_arc(cairo, box->x + box->width / 2, box->y, radius, 0.0, 2.0*SLURP_PI);
	cairo_stroke(cairo);

	// Bottom
	cairo_arc(cairo, box->x + box->width / 2, box->y + box->height, radius, 0.0, 2.0*SLURP_PI);
	cairo_stroke(cairo);

	// Left
	cairo_arc(cairo, box->x, box->y + box->height / 2, radius, 0.0, 2.0*SLURP_PI);
	cairo_stroke(cairo);

	// Right
	cairo_arc(cairo, box->x + box->width, box->y + box->height / 2, radius, 0.0, 2.0*SLURP_PI);
	cairo_stroke(cairo);
}

static void box_layout_to_output(struct slurp_box *box, struct slurp_output *output) {
	box->x -= output->logical_geometry.x;
	box->y -= output->logical_geometry.y;
}

void render(struct slurp_output *output) {
	struct slurp_state *state = output->state;
	struct pool_buffer *buffer = output->current_buffer;
	cairo_t *cairo = buffer->cairo;

	// Clear
	cairo_set_operator(cairo, CAIRO_OPERATOR_SOURCE);
	set_source_u32(cairo, state->colors.background);
	cairo_paint(cairo);

	// Draw option boxes from input
	struct slurp_box *choice_box;
	wl_list_for_each(choice_box, &state->boxes, link) {
		if (box_intersect(&output->logical_geometry,
					choice_box)) {
			struct slurp_box b = *choice_box;
			box_layout_to_output(&b, output);
			draw_rect(cairo, &b, state->colors.choice);
			cairo_fill(cairo);
		}
	}

	struct slurp_seat *seat;
	wl_list_for_each(seat, &state->seats, link) {
		struct slurp_selection *current_selection =
			slurp_seat_current_selection(seat);

		if (!current_selection->has_selection) {
			continue;
		}

		if (!box_intersect(&output->logical_geometry,
			&current_selection->selection)) {
			continue;
		}
		struct slurp_box b = current_selection->selection;
		box_layout_to_output(&b, output);

		draw_rect(cairo, &b, state->colors.selection);
		cairo_fill(cairo);

		// Draw border
		cairo_set_line_width(cairo, state->border_weight);
		draw_rect(cairo, &b, state->colors.border);
		cairo_stroke(cairo);

		if (state->alter_selection) {
			if (state->alter_state != ALTER_STATE_INITIAL) {
				// Draw grabbers in the corners
				draw_grabbers(
					cairo,
					&b,
					state->grabber_radius,
					state->colors.grabber,
					state->colors.grabber_border
				);
			}
		}

		if (state->display_dimensions) {
			cairo_select_font_face(cairo, state->font_family,
					       CAIRO_FONT_SLANT_NORMAL,
					       CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cairo, 14);
			set_source_u32(cairo, state->colors.border);
			// buffer of 12 can hold selections up to 99999x99999
			char dimensions[12];
			snprintf(dimensions, sizeof(dimensions), "%ix%i",
				 b.width, b.height);
			cairo_move_to(cairo, b.x + b.width + 10,
				      b.y + b.height + 20);
			cairo_show_text(cairo, dimensions);
		}
	}
}
