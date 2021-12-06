#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <cairo.h>

typedef struct _ftfont_cairo_mgr ftfont_cairo_mgr_t;
typedef struct _ftfont_cairo_font ftfont_cairo_font_t;

ftfont_cairo_mgr_t *ftfont_cairo_mgr_create();
void ftfont_cairo_mgr_destroy(ftfont_cairo_mgr_t *fcm);

ftfont_cairo_font_t *ftfont_cairo_load(ftfont_cairo_mgr_t *fcm, const char *filename);

void ftfont_cairo_unload(ftfont_cairo_font_t *font);

void ftfont_cairo_set_font(cairo_t *cr, ftfont_cairo_font_t *font, double size);

// len: -1 for strlen(str)
// pkern: 1 enabled, 0 disabled
// gkern: tracking in 1/1000 em
// returns NULL, or must be cairo_glyph_free()d
cairo_glyph_t *ftfont_cairo_get_glyphs(cairo_t *cr, const char *str, int len, double x, double y, int pkern, int gkern, int *ret_num_glyphs); // of current face

#ifdef __cplusplus
};
#endif

