#include <cairo/cairo.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
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

static void draw_chamfered_rect(cairo_t *cairo, struct slurp_box *box, uint32_t color, uint32_t radius) {
	set_source_u32(cairo, color);
	
	int r = radius;

	int half_width = box->width / 2;
	int half_height = box->height / 2;
	if (r > half_width || r > half_height) {
		r = half_width < half_height ? half_width : half_height;
	}

	uint32_t line_dx = box->width - (2 * r);
	uint32_t line_dy = box->height - (2 * r);

	cairo_move_to(cairo, box->x + r, box->y);
	cairo_rel_line_to(cairo, line_dx, 0);
	cairo_rel_line_to(cairo, r, r);
	cairo_rel_line_to(cairo, 0, line_dy);
	cairo_rel_line_to(cairo, -r, r);
	cairo_rel_line_to(cairo, -line_dx, 0);
	cairo_rel_line_to(cairo, -r, -r);
	cairo_rel_line_to(cairo, 0, -line_dy);
	cairo_rel_line_to(cairo, r, -r);

	cairo_close_path(cairo);
}

static void draw_round_rect(cairo_t *cairo, struct slurp_box *box, uint32_t color, uint32_t radius) {
	set_source_u32(cairo, color);

	double x = box->x, y = box->y;
	double w = box->width, h = box->height;
	int r = radius;

	int half_width = box->width / 2;
	int half_height = box->height / 2;
	if (r > half_width || r > half_height) {
		r = half_width < half_height ? half_width : half_height;
	}

	cairo_move_to(cairo, x + r, y);
	cairo_arc(cairo, x + w - r, y + r    , r, -M_PI / 2, 0);
	cairo_arc(cairo, x + w - r, y + h - r, r,  0       , M_PI / 2);
	cairo_arc(cairo, x + r    , y + h - r, r,  M_PI / 2, M_PI);
	cairo_arc(cairo, x + r    , y + r    , r,  M_PI    , 3 * M_PI / 2);
	cairo_close_path(cairo);
}


static void draw_sel_shape(cairo_t *cairo, struct slurp_state *state,
		struct slurp_box *sel_box, uint32_t color) {
	uint32_t abs_h = sel_box->height >= 0 ? sel_box->height : -sel_box->height;
	uint32_t abs_w = sel_box->width >= 0 ? sel_box->width : -sel_box->width;

	bool use_rect = state->border_radius == 0 ||
		abs_h < state->border_weight ||
		abs_w < state->border_weight;
	if (use_rect) {
		draw_rect(cairo, sel_box, color);
	} else if (state->border_chamfered) {
		draw_chamfered_rect(cairo, sel_box, color, state->border_radius);
	} else {
		draw_round_rect(cairo, sel_box, color, state->border_radius);
	}
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

		// Draw selection fill
		draw_sel_shape(cairo, state, sel_box, state->colors.selection);
		cairo_fill(cairo);

		// Draw border
		cairo_set_line_width(cairo, state->border_weight);
		draw_sel_shape(cairo, state, sel_box, state->colors.border);
		cairo_stroke(cairo);

		if (state->display_dimensions) {
			cairo_select_font_face(cairo, state->font_family,
					       CAIRO_FONT_SLANT_NORMAL,
					       CAIRO_FONT_WEIGHT_NORMAL);
			cairo_set_font_size(cairo, 14);
			set_source_u32(cairo, state->colors.border);
			// buffer of 12 can hold selections up to 99999x99999
			char dimensions[12];
			snprintf(dimensions, sizeof(dimensions), "%ix%i",
				 sel_box->width, sel_box->height);
			cairo_move_to(cairo, sel_box->x + sel_box->width + 10,
				      sel_box->y + sel_box->height + 20);
			cairo_show_text(cairo, dimensions);
		}
	}
}
