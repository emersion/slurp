#ifndef _RENDER_H
#define _RENDER_H

#include "slurp.h"
#include "cairo.h"

struct slurp_output;

void render(struct slurp_output *output);

void render_background(struct slurp_output *output);
cairo_pattern_t *create_background_pattern(const char *png_path);

#endif
