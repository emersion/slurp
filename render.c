#include <cairo/cairo.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "pool-buffer.h"
#include "render.h"
#include "slurp.h"

#define LABEL_BOX_PADDING 10
#define LABEL_HORIZONTAL_MARGING 10
#define LABEL_VERTICAL_MARGING 20

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

static void draw_rect_label(cairo_t *cairo, struct slurp_box *box, enum slurp_label_anchor anchor,
		uint32_t text_color, uint32_t background_color, const char *font_family, const char *text) {
	if (anchor == ANCHOR_NONE)
		return;
	cairo_select_font_face(cairo, font_family,
						 CAIRO_FONT_SLANT_NORMAL,
						 CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(cairo, 20);
	cairo_text_extents_t extents;
	cairo_text_extents(cairo, text, &extents);
	struct slurp_box labelbox = { 0 };
	labelbox.width = ceil(extents.width) + 2*LABEL_BOX_PADDING;
	labelbox.height = ceil(extents.height) + 2*LABEL_BOX_PADDING;
	labelbox.x = box->x + (box->width - labelbox.width) / 2;
	labelbox.y = box->y + (box->height - labelbox.height) / 2;

	if (anchor & ANCHOR_TOP && anchor & ANCHOR_BOTTOM) {
		// Nothing to do
	} else if (anchor & ANCHOR_TOP) {
		labelbox.y = box->y + LABEL_VERTICAL_MARGING;
	} else if (anchor & ANCHOR_BOTTOM) {
		labelbox.y = box->y + box->height - LABEL_VERTICAL_MARGING - labelbox.height;
	}
	if (anchor & ANCHOR_LEFT && anchor & ANCHOR_RIGHT) {
		// Nothing to do
	} else if (anchor & ANCHOR_LEFT) {
		labelbox.x = box->x + LABEL_HORIZONTAL_MARGING;
	} else if (anchor & ANCHOR_RIGHT) {
		labelbox.x = box->x + box->width - LABEL_HORIZONTAL_MARGING - labelbox.width;
	}

	draw_rect(cairo, &labelbox, background_color);
	cairo_fill(cairo);
	set_source_u32(cairo, text_color);
	cairo_move_to(cairo, labelbox.x + LABEL_BOX_PADDING,
					labelbox.y + LABEL_BOX_PADDING + ceil(extents.height));
	cairo_show_text(cairo, text);
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
			if (b.label)
				draw_rect_label(cairo, &b, state->label_anchor,
						state->colors.label_text, state->colors.label_background, state->font_family, b.label);
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
		if (b.label)
				draw_rect_label(cairo, &b, state->label_anchor,
						state->colors.label_text, state->colors.label_background, state->font_family, b.label);

		// Draw border
		cairo_set_line_width(cairo, state->border_weight);
		draw_rect(cairo, &b, state->colors.border);
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
				 b.width, b.height);
			cairo_move_to(cairo, b.x + b.width + 10,
				      b.y + b.height + 20);
			cairo_show_text(cairo, dimensions);
		}
	}
}
