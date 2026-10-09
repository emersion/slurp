#include "box.h"

bool box_intersect(const struct slurp_box *a, const struct slurp_box *b) {
	return a->x < b->x + b->width &&
		a->x + a->width > b->x &&
		a->y < b->y + b->height &&
		a->height + a->y > b->y;
}

bool in_box(const struct slurp_box *box, int32_t x, int32_t y) {
	return box->x <= x
		&& box->x + box->width > x
		&& box->y <= y
		&& box->y + box->height > y;
}

int32_t box_size(const struct slurp_box *box) {
	return box->width * box->height;
}

// The border is stroked centered on the box edge, so half of it (rounded up)
// is drawn inside the box. Shrink the box to exclude those pixels.
void box_shrink_by_border(struct slurp_box *box, int32_t border_weight) {
	int32_t inset = (border_weight + 1) / 2;
	if (box->width > 2 * inset) {
		box->x += inset;
		box->width -= 2 * inset;
	}
	if (box->height > 2 * inset) {
		box->y += inset;
		box->height -= 2 * inset;
	}
}
