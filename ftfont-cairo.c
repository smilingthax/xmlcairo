#include "ftfont-cairo.h"

#define WITH_GPOSKERN

#include <ft2build.h>
#include FT_FREETYPE_H

#ifdef WITH_GPOSKERN
#include FT_TRUETYPE_TABLES_H
#include FT_TRUETYPE_TAGS_H
#endif

#include <assert.h>
#include <cairo/cairo-ft.h>

#ifdef WITH_GPOSKERN
#include "gposkern.h"
#endif

struct _ftfont_cairo_mgr {
  FT_Library library;
  size_t num_fonts, size_fonts;
  ftfont_cairo_font_t **fonts;
};

struct _ftfont_cairo_font {
  ftfont_cairo_mgr_t *mgr;
  cairo_font_face_t *fft;

#ifdef WITH_GPOSKERN
  unsigned char *gpos;
  gpos_pair_lookup_t *gposkern;
#endif
};

ftfont_cairo_mgr_t *ftfont_cairo_mgr_create() // {{{
{
  ftfont_cairo_mgr_t *ret = calloc(1, sizeof(ftfont_cairo_mgr_t));
  if (!ret) {
    return NULL;
  }

  FT_Error res = FT_Init_FreeType(&ret->library);
  if (res) {
    fprintf(stderr, "Failed to initialize FreeType: %d\n", res);
    free(ret);
    return NULL;
  }

  return ret;
}
// }}}

void ftfont_cairo_mgr_destroy(ftfont_cairo_mgr_t *fcm) // {{{
{
  if (!fcm) {
    return;
  }

  for (size_t i = 0; i < fcm->num_fonts; i++) {
    fcm->fonts[i]->mgr = NULL;
    cairo_font_face_destroy(fcm->fonts[i]->fft);
  }
  free(fcm->fonts);

  FT_Done_FreeType(fcm->library);    // FIXME ? - assume cairo keeps own reference ?
  free(fcm);
}
// }}}

static void destroy_ftfont_cairo_font(FT_Face face) // {{{
{
  // assert(face && face->generic.data);
  ftfont_cairo_font_t *font = face->generic.data;
  FT_Done_Face(face);
#ifdef WITH_GPOSKERN
  gpos_pair_lookup_destroy(font->gposkern);
  free(font->gpos);
#endif
  free(font);
}
// }}}

static const cairo_user_data_key_t ff_key = {};

static ftfont_cairo_font_t *do_load_font(FT_Library library, const char *filename) // {{{
{
  FT_Face face;
  FT_Error res = FT_New_Face(library, filename, 0, &face);
  if (res || !face) {
   fprintf(stderr, "Could not open Fontfile %s: %d\n", filename, res);
   return NULL;
  }

  ftfont_cairo_font_t *ret = calloc(1, sizeof(ftfont_cairo_font_t));
  if (!ret) {
    FT_Done_Face(face);
    return NULL;
  }

  face->generic.data = ret;
  face->generic.finalizer = NULL; // void (*FT_Generic_Finalizer)(void* object);  // not needed by us

#ifdef WITH_GPOSKERN
  if ((face->face_flags & FT_FACE_FLAG_KERNING) == 0) {
    FT_ULong length = 0;
    if (FT_Load_Sfnt_Table(face, TTAG_GPOS, 0, NULL, &length) == 0) {
      ret->gpos = malloc(length);
      if (!ret->gpos) {
        destroy_ftfont_cairo_font(face);
        return NULL;
      }

      res = FT_Load_Sfnt_Table(face, TTAG_GPOS, 0, ret->gpos, &length);
      if (res) {
        fprintf(stderr, "Failed to load existing GPOS table: %d\n", res);
        destroy_ftfont_cairo_font(face);
        return NULL;
      }

      ret->gposkern = gpos_pair_lookup_create(ret->gpos, length, NULL, NULL);
      if (!ret->gposkern) {
        fprintf(stderr, "gpos_pair_lookup_create returned NULL\n");
        destroy_ftfont_cairo_font(face);
        return NULL;
      }
    }
  }
#endif

  ret->fft = cairo_ft_font_face_create_for_ft_face(face, FT_LOAD_NO_HINTING);
  // cairo_font_face_status() not checked, _set_user_data will. (as per cairo doc example)
  if (cairo_font_face_set_user_data(ret->fft, &ff_key, face, (cairo_destroy_func_t)destroy_ftfont_cairo_font) != CAIRO_STATUS_SUCCESS) {
    cairo_font_face_destroy(ret->fft);
    destroy_ftfont_cairo_font(face);
    return NULL;
  }

  return ret;
}
// }}}

ftfont_cairo_font_t *ftfont_cairo_load(ftfont_cairo_mgr_t *fcm, const char *filename) // {{{
{
  if (!fcm || !filename || !*filename) {
    return NULL;
  }

  if (fcm->num_fonts >= fcm->size_fonts) {
    const size_t new_size = fcm->size_fonts + 20;
    ftfont_cairo_font_t **tmp = realloc(fcm->fonts, new_size * sizeof(ftfont_cairo_font_t *));
    if (!tmp) {
      return NULL;
    }
    fcm->size_fonts += new_size;
    fcm->fonts = tmp;
  }

  ftfont_cairo_font_t *font = do_load_font(fcm->library, filename);
  if (font) {
    fcm->fonts[fcm->num_fonts++] = font;
  }

  return font;
}
// }}}

void ftfont_cairo_unload(ftfont_cairo_font_t *font) // {{{
{
  if (!font) {
    return;
  }

  if (!font->mgr) {
    fprintf(stderr, "Error: Double free of ft_font_cairo_t\n");
    return;
  }
  for (size_t i = 0; i < font->mgr->num_fonts; i++) {
    if (font->mgr->fonts[i] == font) {
      font->mgr->fonts[i] = font->mgr->fonts[--font->mgr->num_fonts];
      break;
    }
  }
  font->mgr = NULL;

  cairo_font_face_destroy(font->fft);
}
// }}}

// TODO? check cr, font
void ftfont_cairo_set_font(cairo_t *cr, ftfont_cairo_font_t *font, double size) // {{{
{
  if (!font) {
    cairo_set_font_face(cr, NULL);  // (just back to default font)
    cairo_set_font_size(cr, size);
    return;
  }

  cairo_set_font_face(cr, font->fft);
  cairo_set_font_size(cr, size);

  cairo_font_options_t *opts = cairo_font_options_create();
  cairo_font_options_set_hint_metrics(opts, CAIRO_HINT_METRICS_OFF);
  cairo_set_font_options(cr, opts);
  cairo_font_options_destroy(opts);
}
// }}}

// must only be used with faces loaded by us!
static double get_kern(FT_Face face, unsigned short firstGID, unsigned short secondGID) // {{{
{
  // assert(face);
  // assert(face->generic.data);

#ifdef WITH_GPOSKERN
  ftfont_cairo_font_t *font = face->generic.data;
  if (font->gpos) {
    const int val = gpos_pair_lookup_get(font->gposkern, firstGID, secondGID);
    return FT_MulFix(val, face->size->metrics.x_scale) / 64.0;  // (double)face->units_per_EM;
  }
#endif

  FT_Vector vec;
//  FT_Error res = FT_Get_Kerning(face, firstGID, secondGID, FT_KERNING_UNSCALED, &vec);
  FT_Error res = FT_Get_Kerning(face, firstGID, secondGID, FT_KERNING_UNFITTED, &vec);  // = UNSCALED w/ *size/units_per_EM, except for rounding
//  FT_Error res = FT_Get_Kerning(face, firstGID, secondGID, FT_KERNING_DEFAULT, &vec);  // like UNFITTED, but rounded to whole pixels
  if (res) {
    return 0.0;
  }

  return vec.x / 64.0;   // DEFAULT/UNFITTED
//  return FT_MulFix(vec.x, face->size->metrics.x_scale); // * size / (double)face->units_per_EM;  // UNSCALED
}
// }}}

// len: -1 for strlen(str)
// pkern: 1 enabled, 0 disabled
// gkern: tracking in 1/1000 em
// returns NULL, or must be cairo_glyph_free()d
cairo_glyph_t *ftfont_cairo_get_glyphs(cairo_t *cr, const char *str, int len, double x, double y, int pkern, int gkern, int *ret_num_glyphs) // {{{
{
  cairo_scaled_font_t *sface = cairo_get_scaled_font(cr);
  if (!sface) {
    return NULL;
  }

  cairo_glyph_t *glyphs = NULL;
  cairo_status_t status = cairo_scaled_font_text_to_glyphs(
    sface,
    x, y,
    str, len,
    &glyphs, ret_num_glyphs,
    NULL, NULL,
    NULL
  );

  if (status != CAIRO_STATUS_SUCCESS || !glyphs) {
    return NULL;
  }

  // assert(cairo_scaled_font_get_type(sface) == CAIRO_FONT_TYPE_FT); //  ?
  // (NOTE: never returns _TOY ...)

  // NOTE: lock_face ensures  _cairo_ft_unscaled_font_set_scale(scaled_font->unscaled, &scaled_font->base.scale)
  //                    i.e.  FT_Set_Char_Size(unscaled->face,  sf.x_scale * 64.0 + .5,  sf.y_scale * 64.0 + .5,  0, 0);
  FT_Face face = cairo_ft_scaled_font_lock_face(sface);
  if (!face) {
    return glyphs; // (also, when type is not _FT ...)
  } else if (!face->generic.data) {
    fprintf(stderr, "Error: Unexpected current font\n");
    cairo_ft_scaled_font_unlock_face(sface);
    return glyphs;
  }

//  const double dgkern = gkern * 64.0*_size/1000 / 64.0;
//  const double dgkern = gkern * face->size->metrics.x_scale / 1000.0 / 64.0;
  const double dgkern = gkern * FT_MulFix(face->units_per_EM, face->size->metrics.x_scale) / 1000.0 / 64.0;

  unsigned int prev = 0;   // TODO? *kernfrom ?
  double acc = 0.0;
  const int num_glyphs = *ret_num_glyphs;
  for (int i = 0; i < num_glyphs; i++) {
    if (prev) {
      if (pkern) {
        acc += get_kern(face, prev, glyphs[i].index);
      }
      acc += dgkern;
    }
    glyphs[i].x += acc;

    prev = glyphs[i].index;
  }

  cairo_ft_scaled_font_unlock_face(sface);

  return glyphs;
}
// }}}

