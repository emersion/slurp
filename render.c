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

void render_background(struct slurp_output *output) {
	struct slurp_state *state = output->state;
	struct slurp_background *bg = &output->background;
	bg->surface = wl_compositor_create_surface(state->compositor);
	bg->subsurface = wl_subcompositor_get_subsurface(state->subcompositor,
			bg->surface, output->surface);
	wl_subsurface_place_below(bg->subsurface, output->surface);
	wl_subsurface_set_position(bg->subsurface, 0, 0);

	int32_t scale = state->max_scale;
	int32_t width = output->width * scale;
	int32_t height = output->height * scale;

	struct pool_buffer *buffer = create_buffer(state->shm, &bg->buffer, width, height);
	if (buffer == NULL) {
		return;
	}

	// Actually draw the background onto the surface:
	// I'm not sure this transformation is correct.
	cairo_translate(buffer->cairo,
			-output->logical_geometry.x * scale, -output->logical_geometry.y * scale);
	cairo_set_source(buffer->cairo, state->background_img);
	cairo_paint(buffer->cairo);

	wl_surface_attach(bg->surface, buffer->buffer, 0, 0);
	wl_surface_damage(bg->surface, 0, 0, output->width, output->height);
	wl_surface_set_buffer_scale(bg->surface, scale);
	wl_surface_commit(bg->surface);
}

cairo_pattern_t *create_background_pattern(const char *png_path) {
	cairo_surface_t *surface = cairo_image_surface_create_from_png(png_path);
	if (surface == NULL || cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
		return NULL;
	}
	cairo_pattern_t *pattern = cairo_pattern_create_for_surface(surface);
	cairo_surface_destroy(surface);
	return pattern;
}
