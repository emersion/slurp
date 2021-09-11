#ifndef _POOL_BUFFER_H
#define _POOL_BUFFER_H

#include <cairo/cairo.h>
#include <stdbool.h>
#include <stdint.h>
#include <wayland-client.h>

struct pool_buffer {
	struct wl_buffer *buffer;
	cairo_surface_t *surface;
	cairo_t *cairo;
	uint32_t width, height;
	double scale;
	void *data;
	size_t size;
	bool busy;
};

struct pool_buffer *get_next_buffer(struct wl_shm *shm,
	struct pool_buffer pool[static 2], uint32_t width, uint32_t height, double scale);
void finish_buffer(struct pool_buffer *buffer);

#endif
