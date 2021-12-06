#include "xmlcairo-int.h"
#include "xmlcairo.h"
#include <cairo.h>
#include <string.h>  // memcpy()
//#include <assert.h>
#include <libxml/xmlIO.h>
#include "ftfont-cairo.h"

#if __has_attribute(unused)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

static cairo_status_t xmlioCairoReadFunc(void *closure, unsigned char *data, unsigned int length) // {{{
{
  xmlParserInputBufferPtr ibuf = (xmlParserInputBufferPtr)closure;

  size_t len = xmlBufUse(ibuf->buffer);
  if (len >= length) {
    memcpy(data, xmlBufContent(ibuf->buffer), length);
    xmlBufShrink(ibuf->buffer, length);
    return CAIRO_STATUS_SUCCESS;
  }

  const int res = xmlParserInputBufferRead(ibuf, length - len);  // (note: xmlio will at least read some internal MINLEN)
  if (res <= 0) {
    return CAIRO_STATUS_READ_ERROR;
  }

  len = xmlBufUse(ibuf->buffer);
  if (len < length) { // - any short read is an error, because cairo only uses this read func to parse pngs, and their size is known from the header
    return CAIRO_STATUS_READ_ERROR;
  }
  memcpy(data, xmlBufContent(ibuf->buffer), length);
  xmlBufShrink(ibuf->buffer, length);

  return CAIRO_STATUS_SUCCESS;
}
// }}}

// NULL on error
static cairo_surface_t *_xmlcairo_surface_read_png_file(const char *filename) // {{{
{
  xmlParserInputBufferPtr ibuf = xmlParserInputBufferCreateFilename(filename, XML_CHAR_ENCODING_NONE);
  if (!ibuf) {
    return NULL;  // TODO? create nil surface with error ?
  }

  cairo_surface_t *surface = cairo_image_surface_create_from_png_stream(xmlioCairoReadFunc, ibuf);
  // assert(tsfc);
  xmlFreeParserInputBuffer(ibuf);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return NULL;
  }
  return surface;
}
// }}}

static cairo_status_t xmlioCairoWriteFunc(void *closure, const unsigned char *data, unsigned int length) // {{{
{
  xmlOutputBufferPtr obuf = (xmlOutputBufferPtr)closure;

  if (xmlOutputBufferWrite(obuf, length, (const char *)data) < 0) {
    return CAIRO_STATUS_WRITE_ERROR;
  }
  return CAIRO_STATUS_SUCCESS;
}
// }}}


static void hash_free_imgs(void *entry, const xmlChar *name UNUSED) // {{{
{
  cairo_surface_destroy((cairo_surface_t *)entry);
}
// }}}


static xmlcairo_surface_t *_xmlcairo_surface_alloc() // {{{
{
  xmlcairo_surface_t *ret = calloc(1, sizeof(xmlcairo_surface_t));

  ret->imgs = xmlHashCreate(32);
  if (!ret->imgs) {
    free(ret);
    return NULL;
  }

  ret->fontfiles = xmlHashCreate(32);
  if (!ret->fontfiles) {
    xmlHashFree(ret->imgs, hash_free_imgs); // (note: hash_free_imgs not really needed yet, no entries)
    free(ret);
    return NULL;
  }

  ret->fonts = xmlHashCreate(32);
  if (!ret->fonts) {
    xmlHashFree(ret->imgs, hash_free_imgs);
    xmlHashFree(ret->fontfiles, NULL);
    free(ret);
    return NULL;
  }

  return ret;
}
// }}}

void _xmlcairo_surface_free(xmlcairo_surface_t *surface) // {{{
{
  // assert(surface);

  xmlHashFree(surface->fonts, NULL);
  xmlHashFree(surface->fontfiles, NULL);
  if (surface->fmgr) {
    ftfont_cairo_mgr_destroy(surface->fmgr);
  }

  xmlHashFree(surface->imgs, hash_free_imgs);

  free(surface);
}
// }}}

cairo_status_t xmlcairo_surface_destroy(xmlcairo_surface_t *surface) // {{{
{
  if (!surface) {
    return CAIRO_STATUS_NULL_POINTER;
  }

  cairo_status_t ret = cairo_surface_status(surface->surface);
  if (ret == CAIRO_STATUS_SUCCESS) {
    if (cairo_surface_get_type(surface->surface) == CAIRO_SURFACE_TYPE_IMAGE) {
      ret = cairo_surface_write_to_png_stream(surface->surface, xmlioCairoWriteFunc, surface->obuf);
    }
    cairo_surface_destroy(surface->surface);
  }

  xmlOutputBufferClose(surface->obuf);

  _xmlcairo_surface_free(surface);
  return ret;
}
// }}}

static xmlcairo_surface_t *_xmlcairo_surface_alloc_file(const char *filename) // {{{
{
  xmlcairo_surface_t *ret = _xmlcairo_surface_alloc();
  if (!ret) {
    return NULL;
  }

  ret->obuf = xmlOutputBufferCreateFilename(filename, NULL, 0);
  if (!ret->obuf) {
    _xmlcairo_surface_free(ret);
    return NULL;
  }

  return ret;
}
// }}}

#if 1    // CAIRO_HAS_PDF_SURFACE
#include <cairo-pdf.h>

xmlcairo_surface_t *xmlcairo_surface_create_pdf(const char *filename, double width_in_points, double height_in_points) // {{{
{
  xmlcairo_surface_t *ret = _xmlcairo_surface_alloc_file(filename);
  if (!ret) {
    return NULL;
  }

  ret->surface = cairo_pdf_surface_create_for_stream(xmlioCairoWriteFunc, ret->obuf, width_in_points, height_in_points);
  // assert(ret->surface);
  if (cairo_surface_status(ret->surface) != CAIRO_STATUS_SUCCESS) {
    xmlcairo_surface_destroy(ret);
    return NULL;
  }

  return ret;
}
// }}}
#endif

// assert(CAIRO_HAS_IMAGE_SURFACE); ...
xmlcairo_surface_t *xmlcairo_surface_create_png(const char *filename, cairo_format_t format, int width, int height) // {{{
{
  xmlcairo_surface_t *ret = _xmlcairo_surface_alloc_file(filename);
  if (!ret) {
    return NULL;
  }

  ret->surface = cairo_image_surface_create(format, width, height);
  // assert(ret->surface);
  if (cairo_surface_status(ret->surface) != CAIRO_STATUS_SUCCESS) {
    xmlcairo_surface_destroy(ret);
    return NULL;
  }

  return ret;
}
// }}}

#if 1    // CAIRO_HAS_PS_SURFACE
#include <cairo-ps.h>

xmlcairo_surface_t *xmlcairo_surface_create_ps(const char *filename, double width_in_points, double height_in_points) // {{{
{
  xmlcairo_surface_t *ret = _xmlcairo_surface_alloc_file(filename);
  if (!ret) {
    return NULL;
  }

  ret->surface = cairo_ps_surface_create_for_stream(xmlioCairoWriteFunc, ret->obuf, width_in_points, height_in_points);
  // assert(ret->surface);
  if (cairo_surface_status(ret->surface) != CAIRO_STATUS_SUCCESS) {
    xmlcairo_surface_destroy(ret);
    return NULL;
  }

  return ret;
}
// }}}
#endif

#if 1    // CAIRO_HAS_SVG_SURFACE
#include <cairo-svg.h>

xmlcairo_surface_t *xmlcairo_surface_create_svg(const char *filename, double width_in_points, double height_in_points) // {{{
{
  xmlcairo_surface_t *ret = _xmlcairo_surface_alloc_file(filename);
  if (!ret) {
    return NULL;
  }

  ret->surface = cairo_svg_surface_create_for_stream(xmlioCairoWriteFunc, ret->obuf, width_in_points, height_in_points);
  // assert(ret->surface);
  if (cairo_surface_status(ret->surface) != CAIRO_STATUS_SUCCESS) {
    xmlcairo_surface_destroy(ret);
    return NULL;
  }

  return ret;
}
// }}}
#endif

#if 1    // if CAIRO_HAS_SCRIPT_SURFACE
#include <cairo-script.h>

xmlcairo_surface_t *xmlcairo_surface_create_script(const char *filename, cairo_content_t content, double width, double height) // {{{
{
  xmlcairo_surface_t *ret = _xmlcairo_surface_alloc_file(filename);
  if (!ret) {
    return NULL;
  }

  cairo_device_t *device = cairo_script_create_for_stream(xmlioCairoWriteFunc, ret->obuf);
  ret->surface = cairo_script_surface_create(device, content, width, height); // NOTE: will handle (cairo_device_status(device) != SUCCESS)
  cairo_device_destroy(device);
  // assert(ret->surface);
  if (cairo_surface_status(ret->surface) != CAIRO_STATUS_SUCCESS) {
    xmlcairo_surface_destroy(ret);
    return NULL;
  }

  return ret;
}
// }}}
#endif

// --

cairo_status_t xmlcairo_load_image(xmlcairo_surface_t *surface, const char *key, const char *filename) // {{{
{
  if (!surface || !key || !filename) {
    return CAIRO_STATUS_NULL_POINTER;
  }

  cairo_surface_t *img = _xmlcairo_surface_read_png_file(filename);
  if (!img) {
    return CAIRO_STATUS_READ_ERROR;  // TODO?
  }

  if (xmlHashUpdateEntry(surface->imgs, (const xmlChar *)key, img, hash_free_imgs) != 0) {
    cairo_surface_destroy(img);
    return CAIRO_STATUS_NO_MEMORY;
  }

  return CAIRO_STATUS_SUCCESS;
}
// }}}

cairo_status_t xmlcairo_load_font(xmlcairo_surface_t *surface, const char *key, const char *filename) // {{{
{
  if (!surface || !key || !filename) {
    return CAIRO_STATUS_NULL_POINTER;
  }

  if (!surface->fmgr) {
    surface->fmgr = ftfont_cairo_mgr_create();
    if (!surface->fmgr) {
      return CAIRO_STATUS_NO_MEMORY;  // FIXME? FT init failed ...
    }
  }

  ftfont_cairo_font_t *font = xmlHashLookup(surface->fontfiles, (const xmlChar *)filename);
  if (!font) {
    font = ftfont_cairo_load(surface->fmgr, filename);
    if (!font) {
      return CAIRO_STATUS_NO_MEMORY;  // FIXME? font load failed ...
    }
    if (xmlHashUpdateEntry(surface->fontfiles, (const xmlChar *)filename, font, NULL) != 0) {
      ftfont_cairo_unload(font);  // (TODO? really needed?)
      return CAIRO_STATUS_NO_MEMORY;
    }
  }

  if (xmlHashUpdateEntry(surface->fonts, (const xmlChar *)key, font, NULL) != 0) {
    return CAIRO_STATUS_NO_MEMORY;
  }

  return CAIRO_STATUS_SUCCESS;
}
// }}}

