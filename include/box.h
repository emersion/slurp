#ifndef _BOX_H
#define _BOX_H

#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

struct slurp_box {
	int32_t x, y;
	int32_t width, height;
	char *label;
	struct wl_list link;
};

bool box_intersect(const struct slurp_box *a, const struct slurp_box *b);

bool in_box(const struct slurp_box *box, int32_t x, int32_t y);

int32_t box_size(const struct slurp_box *box);

#endif
