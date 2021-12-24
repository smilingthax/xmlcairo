#pragma once

// ... #include <cairo.h>
typedef struct _cairo cairo_t;
typedef struct _cairo_matrix cairo_matrix_t;

// returns < 0 on success, otherwise: position of first problematic character
int apply_svg_cairo_path(cairo_t *cr, const char *path);

int parse_svg_cairo_transform(cairo_matrix_t *matrix, const char *str);

struct cairo_svg_dasharray_s {
  double *dashes;
  int num_dashes;
};
int parse_svg_cairo_dasharray(struct cairo_svg_dasharray_s *ret, const char *str); // *ret shall be zero-initialized
void free_dasharray(struct cairo_svg_dasharray_s *da);

