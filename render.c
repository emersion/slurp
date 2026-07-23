#include <cairo/cairo.h>
#include <stdio.h>
#include <stdlib.h>

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
			draw_rect(cairo, choice_box, state->colors.choice);
			cairo_fill(cairo);
		}
	}

	struct slurp_seat *seat;
	wl_list_for_each(seat, &state->seats, link) {
		struct slurp_selection *current_selection =
			slurp_seat_current_selection(seat);

		if (!current_selection->has_selection && state->crosshairs) {
			struct slurp_box *output_box = &output->logical_geometry;
			if (in_box(output_box, current_selection->x, current_selection->y)) {

				set_source_u32(cairo, state->colors.border);
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
		struct slurp_box *sel_box = &current_selection->selection;

		draw_rect(cairo, sel_box, state->colors.selection);
		cairo_fill(cairo);

		// Draw border
		cairo_set_line_width(cairo, state->border_weight);
		draw_rect(cairo, sel_box, state->colors.border);
		cairo_stroke(cairo);

		if (state->display_dimensions) {
			cairo_select_font_face(cairo, state->font_family,
					       CAIRO_FONT_SLANT_NORMAL,
					       CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cairo, 14);
			// buffer of 12 can hold selections up to 99999x99999
			char dimensions[12];
			snprintf(dimensions, sizeof(dimensions), "%ix%i",
				 sel_box->width, sel_box->height);
			cairo_text_extents_t extents;
			cairo_text_extents(cairo, dimensions, &extents);

			double padding = 4.0;
			double text_x, text_y;

			switch (state->text_alignment) {
				case SLURP_ALIGN_CENTER:
					text_x = sel_box->x + (sel_box->width - extents.width) / 2;
					text_y = sel_box->y + (sel_box->height + extents.height) / 2;
					break;
				case SLURP_ALIGN_TOP_LEFT:
					text_x = sel_box->x - extents.width - padding;
					text_y = sel_box->y - padding;
					break;
				case SLURP_ALIGN_TOP_RIGHT:
					text_x = sel_box->x + sel_box->width + padding;
					text_y = sel_box->y - padding;
					break;
				case SLURP_ALIGN_BOTTOM_LEFT:
					text_x = sel_box->x - extents.width - padding;
					text_y = sel_box->y + sel_box->height + padding + extents.height;
					break;
				case SLURP_ALIGN_BOTTOM_RIGHT:
				default:
					text_x = sel_box->x + sel_box->width + padding;
					text_y = sel_box->y + sel_box->height + padding + extents.height;
					break;
			}

			// Draw background box
			set_source_u32(cairo, state->colors.text_bg);
			cairo_rectangle(cairo, 
					text_x - padding, 
					text_y - extents.height - padding, 
					extents.width + padding * 2, 
					extents.height + padding * 2);
			cairo_fill(cairo);

			// Draw text
			if (state->has_text_color) {
				set_source_u32(cairo, state->colors.text);
			} else {
				set_source_u32(cairo, state->colors.border);
			}
			cairo_move_to(cairo, text_x, text_y);
			cairo_show_text(cairo, dimensions);
		}
	}
}
