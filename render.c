#include <cairo/cairo.h>
#include <pango/pangocairo.h>
#include <stdio.h>

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

		if (state->display_dimensions) {
			set_source_u32(cairo, state->colors.border);
			// buffer of 12 can hold selections up to 99999x99999
			char dimensions[12];
			snprintf(dimensions, sizeof(dimensions), "%ix%i", b.width, b.height);

			PangoLayout *layout = pango_cairo_create_layout (cairo);
			PangoFontDescription *desc = pango_font_description_new();
			pango_font_description_set_family_static(desc, state->font_family);
			pango_font_description_set_size(desc, 11 * PANGO_SCALE);
			pango_font_description_set_style(desc, PANGO_STYLE_NORMAL);
			pango_font_description_set_weight(desc, PANGO_WEIGHT_NORMAL);
			pango_layout_set_font_description (layout, desc);
			pango_font_description_free (desc);

			cairo_move_to(cairo, b.x + b.width + 12, b.y + b.height + 8);
			pango_layout_set_text (layout, dimensions, -1);
			pango_cairo_show_layout (cairo, layout);
		}
	}
}
