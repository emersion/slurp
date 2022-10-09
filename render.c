#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pool-buffer.h"
#include "render.h"
#include "slurp.h"

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
	cairo_text_extents_t extents;
	wl_list_for_each(choice_box, &state->boxes, link) {
		if (box_intersect(&output->logical_geometry,
					choice_box)) {
			struct slurp_box b = *choice_box;
			box_layout_to_output(&b, output);
			draw_rect(cairo, &b, state->colors.choice);
			cairo_fill(cairo);

			// Draw label
			if (state->display_labels && b.label) {

				cairo_select_font_face(cairo, state->font_family,
									 CAIRO_FONT_SLANT_NORMAL,
									 CAIRO_FONT_WEIGHT_NORMAL);
				cairo_set_font_size(cairo, state->font_size);
				cairo_text_extents(cairo, b.label, &extents);
				set_source_u32(cairo, state->colors.font);
				cairo_move_to(cairo, b.x + (b.width - extents.width) / 2.0,
								b.y + b.height / 2.0);
				cairo_show_text(cairo, b.label);
			}
		}
	}

	struct slurp_seat *seat;
	wl_list_for_each(seat, &state->seats, link) {
		struct slurp_selection *current_selection =
			slurp_seat_current_selection(seat);

		if (!current_selection->has_selection && state->crosshair) {
			struct slurp_box *output_box = &output->logical_geometry;
			if (in_box(output_box, current_selection->x, current_selection->y)) {

				set_source_u32(cairo, state->colors.crosshair);
				cairo_rectangle(cairo, output_box->x, current_selection->y, output->logical_geometry.width, 1);
				cairo_fill(cairo);
				cairo_rectangle(cairo, current_selection->x, output->logical_geometry.y, 1, output->logical_geometry.height);
				cairo_fill(cairo);
			}
		}

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

		// Draw label
		if (state->display_labels && b.label) {

			cairo_select_font_face(cairo, state->font_family,
								 CAIRO_FONT_SLANT_NORMAL,
								 CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cairo, state->font_size);
			cairo_text_extents(cairo, b.label, &extents);
			set_source_u32(cairo, state->colors.choice_font);
			cairo_move_to(cairo, b.x + (b.width - extents.width) / 2.0,
							b.y + b.height / 2.0);
			cairo_show_text(cairo, b.label);
		}

		if (state->display_dimensions) {
			cairo_select_font_face(cairo, state->font_family,
					       CAIRO_FONT_SLANT_NORMAL,
					       CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cairo, state->font_size);
			set_source_u32(cairo, state->colors.font);
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
