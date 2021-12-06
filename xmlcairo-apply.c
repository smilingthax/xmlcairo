#include "xmlcairo-int.h"
#include <cairo.h>
#include <assert.h>
#include <string.h>  // strlen() can be inlined by compilers
#include <math.h>
#include <libxml/tree.h>
#include "parse-svg-cairo.h"
#include "ftfont-cairo.h"

#if __has_attribute(unused)
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

typedef struct _xmlcairo_surface_t xmlcairo_surface_t;

typedef int (*for_each_attr_fn_t)(const xmlChar *name, const xmlChar *value, void *user);
typedef int (*for_content_fn_t)(const xmlChar *value, void *user);

// NOTE: xmlHasProp also searches DTD, this does not
// returns first fn() returning !=0
static int for_each_attr(xmlNodePtr node, for_each_attr_fn_t fn, void *user) // {{{
{
  int ret;
  for (xmlAttrPtr attr = node->properties; attr; attr = attr->next) {
    if (attr->type != XML_ATTRIBUTE_NODE) {
      continue;
    }
    if (attr->children && !attr->children->next &&
        (attr->children->type == XML_TEXT_NODE ||
         attr->children->type == XML_CDATA_SECTION_NODE)) {
      ret = fn(attr->name, attr->children->content, user);
    } else {
      xmlChar *val = xmlNodeGetContent((const xmlNode *)attr);  // could be NULL, just pass to fn...
      ret = fn(attr->name, val, user);
      xmlFree(val);
    }
    if (ret != 0) {
      return ret;
    }
  }
  return 0;
}
// }}}

// only for XML_ELEMENT_NODEs
static int for_content(xmlNodePtr node, for_content_fn_t fn, void *user) // {{{
{
  assert(node->type == XML_ELEMENT_NODE);

  if (!node->children) {
    return fn((const xmlChar *)"", user);
  }

  if (!node->children->next &&
      (node->children->type == XML_TEXT_NODE ||
       node->children->type == XML_CDATA_SECTION_NODE)) {
    return fn(node->children->content, user);  // TODO? replace (!node->children->content) w/ (const xmlChar *)"" ?
  }

  xmlChar *str = xmlNodeGetContent(node);  // could be NULL, just pass to fn
  const int ret = fn(str, user);
  xmlFree(str);
  return ret;
}
// }}}

static inline int strEqual(const xmlChar *a, const char *b) // {{{
{
  return xmlStrEqual(a, (const xmlChar *)b);
}
// }}}

// --

// "true"/"1" or "false"/"0"
static int parse_bool(const xmlChar *str) // {{{ -1 on error
{
  // (SVG spec: only "true" or "false"; everything else: falsy)
  if (strEqual(str, "true") || strEqual(str, "1")) {
    return 1;
  } else if (strEqual(str, "false") || strEqual(str, "0")) {  // TODO? also for "" and (!str) ?
    return 0;
  } else {
    return -1;
  }
}
// }}}

static double parse_double(const xmlChar *str) // {{{ or NAN
{
  assert(str);  // TODO?
// if (!str) return NAN;  // TODO?
  char *tmp;
  double ret = strtod((const char *)str, &tmp);
  if (*tmp) {
    return NAN;
  }
  return ret;
}
// }}}

static cairo_content_t parse_content(const xmlChar *str) // {{{ or -1
{
  if (strEqual(str, "color")) {
    return CAIRO_CONTENT_COLOR;
  } else if (strEqual(str, "alpha")) {
    return CAIRO_CONTENT_ALPHA;
  } else if (strEqual(str, "coloralpha")) {
    return CAIRO_CONTENT_COLOR_ALPHA;
  } else {
    return -1;
  }
}
// }}}

static cairo_antialias_t parse_antialias(const xmlChar *str) // {{{ or -1
{
  if (strEqual(str, "default")) {
    return CAIRO_ANTIALIAS_DEFAULT;
  } else if (strEqual(str, "none")) {
    return CAIRO_ANTIALIAS_NONE;
  } else if (strEqual(str, "gray")) {
    return CAIRO_ANTIALIAS_GRAY;
  } else if (strEqual(str, "fast")) {
    return CAIRO_ANTIALIAS_FAST;
  } else if (strEqual(str, "good")) {
    return CAIRO_ANTIALIAS_GOOD;
  } else if (strEqual(str, "best")) {
    return CAIRO_ANTIALIAS_BEST;
  } else {
    return -1;
  }
}
// }}}

static cairo_fill_rule_t parse_fill_rule(const xmlChar *str) // {{{ or -1
{
  if (strEqual(str, "winding") || strEqual(str, "nonzero")) {
    return CAIRO_FILL_RULE_WINDING;
  } else if (strEqual(str, "evenodd")) {
    return CAIRO_FILL_RULE_EVEN_ODD;
  } else {
    return -1;
  }
}
// }}}

static cairo_line_cap_t parse_line_cap(const xmlChar *str) // {{{ or -1
{
  if (strEqual(str, "butt")) {
    return CAIRO_LINE_CAP_BUTT;
  } else if (strEqual(str, "round")) {
    return CAIRO_LINE_CAP_ROUND;
  } else if (strEqual(str, "square")) {
    return CAIRO_LINE_CAP_SQUARE;
  } else {
    return -1;
  }
}
// }}}

static cairo_line_join_t parse_line_join(const xmlChar *str) // {{{ or -1
{
  if (strEqual(str, "miter")) {
    return CAIRO_LINE_JOIN_MITER;
  } else if (strEqual(str, "round")) {
    return CAIRO_LINE_JOIN_ROUND;
  } else if (strEqual(str, "bevel")) {
    return CAIRO_LINE_JOIN_BEVEL;
  } else {
    return -1;
  }
}
// }}}

static cairo_operator_t parse_operator(const xmlChar *str) // {{{ or -1
{
  if (!str) {
    return -1;
  }

#define EQ(s) strEqual(str, s)   // NOTE: (str+1,s+1) would be enough, but is probably less efficient (alignment)
#define PREFIX(s) (xmlStrncmp(str, (const xmlChar *)s, strlen(s)) == 0)
  switch (*str) {
  case 'a':
    if (EQ("atop")) return CAIRO_OPERATOR_ATOP;
    else if (EQ("add")) return CAIRO_OPERATOR_ADD;
    return -1;

  case 'c':
    if (EQ("clear")) return CAIRO_OPERATOR_CLEAR;
    else if (PREFIX("color")) {
      if (!str[5]) return CAIRO_OPERATOR_HSL_COLOR;  // i.e. allow both "hsl-color" and "color"
      else if (str[5] != '-') return -1;
      else if (strEqual(str + 6, "dodge")) return CAIRO_OPERATOR_COLOR_DODGE;
      else if (strEqual(str + 6, "burn")) return CAIRO_OPERATOR_COLOR_BURN;
    }
    return -1;

  case 'd':
    if (EQ("darken")) return CAIRO_OPERATOR_DARKEN;
    else if (EQ("default")) return CAIRO_OPERATOR_OVER;  // cairo default
    else if (EQ("difference")) return CAIRO_OPERATOR_DIFFERENCE;
    else if (PREFIX("dest")) {
      if (!str[4]) return CAIRO_OPERATOR_DEST;
      else if (str[4] != '-') return -1;
      else if (strEqual(str + 5, "over")) return CAIRO_OPERATOR_DEST_OVER;
      else if (strEqual(str + 5, "in")) return CAIRO_OPERATOR_DEST_IN;
      else if (strEqual(str + 5, "out")) return CAIRO_OPERATOR_DEST_OUT;
      else if (strEqual(str + 5, "atop")) return CAIRO_OPERATOR_DEST_ATOP;
    }
    return -1;

  case 'e':
    if (EQ("exclusion")) return CAIRO_OPERATOR_EXCLUSION;
    return -1;

  case 'h':
    if (EQ("hard-light")) return CAIRO_OPERATOR_HARD_LIGHT;
    else if (EQ("hue")) return CAIRO_OPERATOR_HSL_HUE;  // i.e. allow both "hsl-hue" and "hue"
    else if (PREFIX("hsl-")) {
      if (strEqual(str + 4, "hue")) return CAIRO_OPERATOR_HSL_HUE;
      else if (strEqual(str + 4, "saturation")) return CAIRO_OPERATOR_HSL_SATURATION;
      else if (strEqual(str + 4, "color")) return CAIRO_OPERATOR_HSL_COLOR;
      else if (strEqual(str + 4, "luminosity")) return CAIRO_OPERATOR_HSL_LUMINOSITY;
    }
    return -1;

  case 'i':
    if (EQ("in")) return CAIRO_OPERATOR_IN;
    return -1;

  case 'l':
    if (EQ("lighten")) return CAIRO_OPERATOR_LIGHTEN;
    else if (EQ("luminosity")) return CAIRO_OPERATOR_HSL_LUMINOSITY;  // i.e. allow both "hsl-luminosity" and "luminosity"
    return -1;

  case 'm':
    if (EQ("multiply")) return CAIRO_OPERATOR_MULTIPLY;
    return -1;

  case 'n':
    if (EQ("normal")) return CAIRO_OPERATOR_OVER;  // = default = over
    return -1;

  case 'o':
    if (EQ("over")) return CAIRO_OPERATOR_OVER;
    else if (EQ("out")) return CAIRO_OPERATOR_OUT;
    else if (EQ("overlay")) return CAIRO_OPERATOR_OVERLAY;
    return -1;

  case 's':
    if (EQ("source")) return CAIRO_OPERATOR_SOURCE;
    else if (EQ("saturate")) return CAIRO_OPERATOR_SATURATE;
    else if (EQ("saturation")) return CAIRO_OPERATOR_HSL_SATURATION;  // i.e. allow both "hsl-saturation" and "saturation"
    else if (EQ("screen")) return CAIRO_OPERATOR_SCREEN;
    else if (EQ("soft-light")) return CAIRO_OPERATOR_SOFT_LIGHT;
    return -1;

  case 'x':
    if (EQ("xor")) return CAIRO_OPERATOR_XOR;
    return -1;

  default:
    return -1;
  }
#undef EQ
}
// }}}

enum gravity_e {
  GRAVITY_CENTER = 0x00,
  GRAVITY_N = 0x10, GRAVITY_NE = 0x12,
  GRAVITY_E = 0x02, GRAVITY_SE = 0x22,
  GRAVITY_S = 0x20, GRAVITY_SW = 0x21,
  GRAVITY_W = 0x01, GRAVITY_NW = 0x11,
  GRAVITY_STRETCH = 0x44
};

enum gravity_e parse_gravity(const xmlChar *str) // {{{ or -1
{
  if (!str) {
    return -1;
  }
  switch (str[0] | 0x20) {
  case 'n':
    if (!str[1]) return GRAVITY_N;
    else if ((str[1] | 0x20) == 'w') return GRAVITY_NW;
    else if ((str[1] | 0x20) == 'e') return GRAVITY_NE;
    break;

  case 's':
    if (!str[1]) return GRAVITY_S;
    else if ((str[1] | 0x20) == 'w') return GRAVITY_SW;
    else if ((str[1] | 0x20) == 'e') return GRAVITY_SE;
    else if (xmlStrcasecmp(str, (const xmlChar *)"stretch") == 0) return GRAVITY_STRETCH;
    break;

  case 'e':
    if (!str[1]) return GRAVITY_E;
    break;

  case 'w':
    if (!str[1]) return GRAVITY_W;
    break;

  case 'c':
    if (xmlStrcasecmp(str, (const xmlChar *)"center") == 0) return GRAVITY_CENTER;
    break;
  }
  return -1;
}
// }}}

// NOTE: treats dst_w/h = 0.0 or NAN as "not present"
static void compute_fit(double src_w, double src_h, double dst_w, double dst_h, enum gravity_e gravity, // {{{
                        double *ret_sx, double *ret_sy, double *ret_dx, double *ret_dy)
{
  // assert(ret_sx && ret_sy && ret_dx && ret_dy);
  if (isnan(dst_w) || dst_w == 0.0) {
    if (isnan(dst_h) || dst_h == 0.0) {
      *ret_sx = *ret_sy = 1.0;
    } else {
      *ret_sx = *ret_sy = src_h / dst_h;
    }
    *ret_dx = *ret_dy = 0.0;

  } else if (isnan(dst_h) || dst_h == 0.0) {
    *ret_sx = *ret_sy = src_w / dst_w;
    *ret_dx = *ret_dy = 0.0;

  } else if (gravity == GRAVITY_STRETCH) {
    *ret_sx = src_w / dst_w;
    *ret_sy = src_h / dst_h;
    *ret_dx = *ret_dy = 0.0;

  } else {
    if (dst_w * src_h > dst_h * src_w) {
      const double xpos = (gravity & GRAVITY_W) ? 0.0 : (gravity & GRAVITY_E) ? 1.0 : 0.5;
      *ret_sx = *ret_sy = src_h / dst_h;
      *ret_dx = xpos * (dst_w - (src_w / *ret_sx));
      *ret_dy = 0;

    } else {
      const double ypos = (gravity & GRAVITY_N) ? 0.0 : (gravity & GRAVITY_S) ? 1.0 : 0.5;
      *ret_sx = *ret_sy = src_w / dst_w;
      *ret_dx = 0;
      *ret_dy = ypos * (dst_h - (src_h / *ret_sy));
    }
  }
}
// }}}

// ---

enum {
  ATTR_SUCCESS = 0,
  ATTR_UNKNOWN,
  ATTR_PARSE,
  ATTR_NOT_FOUND   // patternname / imagename / fontname / ...
};

enum {
  ELEM_SUCCESS,
  ELEM_NOT_ELEMENT,
  ELEM_UNKNOWN,
  ELEM_CAIRO_ERROR,
  ELEM_BADATTR   // TODO?  ELEM_ATTR_UNKNOWN = ATTR_UNKNOWN,  ELEM_ATTR_PARSE = ATTR_PARSE   ??
};

#define WARN(s, ...)  fprintf(stderr, "Warning: " s "\n" ,## __VA_ARGS__);

static int no_attrs(const xmlChar *name, const xmlChar *value UNUSED, void *user UNUSED) // {{{
{
  WARN("expected no attributes, got @%s", name);
  return ATTR_UNKNOWN;
}
// }}}

static int preserve_attrs(const xmlChar *name, const xmlChar *value, void *user) // {{{
{
  if (!strEqual(name, "preserve")) {
    WARN("expected @preserve, got @%s", name);
    return ATTR_UNKNOWN;
  }

  int *preserve = (int *)user;

  const int ret = parse_bool(value);
  if (ret == -1) {
    // WARN(...);   // TODO?!
    return ATTR_PARSE;
  }

  *preserve = ret;
  return ATTR_SUCCESS;
}
// }}}

static int alpha_attrs(const xmlChar *name, const xmlChar *value, void *user) // {{{
{
  if (!strEqual(name, "alpha")) {
    WARN("expected @alpha, got @%s", name);
    return ATTR_UNKNOWN;
  }

  double *alpha = (double *)user;

  const double ret = parse_double(value);
  if (isnan(ret)) {
    // WARN(...);   // TODO?!
    return ATTR_PARSE;
  }

  *alpha = ret;
  return ATTR_SUCCESS;
}
// }}}

static int apply_path_attrs(const xmlChar *name, const xmlChar *value, void *user) // {{{
{
  cairo_t *cr = (cairo_t *)user;

  if (!strEqual(name, "d")) {
    WARN("expected @d, got @%s", name);
    return ATTR_UNKNOWN;
  }

  const int res = apply_svg_cairo_path(cr, (const char *)value);
  if (res >= 0) {
    WARN("could not parse <path d=...%s\"", value + res);
    return ATTR_PARSE;
  }
  return ATTR_SUCCESS;
}
// }}}

static int apply_transform_attrs(const xmlChar *name, const xmlChar *value, void *user)
{
  cairo_t *cr = (cairo_t *)user;

  if (!strEqual(name, "transform")) {
    WARN("expected @transform, got @%s", name);
    return ATTR_UNKNOWN;
  }

  cairo_matrix_t mtx;
  cairo_matrix_init_identity(&mtx);
  const int res = parse_svg_cairo_transform(&mtx, (const char *)value);
  if (res >= 0) {
    WARN("could not parse transform=...%s\"", value + res);
    return ATTR_PARSE;
  }
  cairo_transform(cr, &mtx);
  return ATTR_SUCCESS;
}

static int apply_set_attrs(const xmlChar *name, const xmlChar *value, void *user) // {{{
{
  cairo_t *cr = (cairo_t *)user;

  if (strEqual(name, "antialias")) {
    const cairo_antialias_t val = parse_antialias(value);
    if (val == (cairo_antialias_t)-1) {
      goto err_parse;
    }
    cairo_set_antialias(cr, val);

  } else if (strEqual(name, "fill-rule")) {
    const cairo_fill_rule_t val = parse_fill_rule(value);
    if (val == (cairo_fill_rule_t)-1) {
      goto err_parse;
    }
    cairo_set_fill_rule(cr, val);

  } else if (strEqual(name, "line-cap")) {
    const cairo_line_cap_t val = parse_line_cap(value);
    if (val == (cairo_line_cap_t)-1) {
      goto err_parse;
    }
    cairo_set_line_cap(cr, val);

  } else if (strEqual(name, "line-join")) {
    const cairo_line_join_t val = parse_line_join(value);
    if (val == (cairo_line_join_t)-1) {
      goto err_parse;
    }
    cairo_set_line_join(cr, val);

  } else if (strEqual(name, "line-width")) {
    const double val = parse_double(value);
    if (isnan(val)) {
      goto err_parse;
    }
    cairo_set_line_width(cr, (val < 0.0) ? 0.0 : val);

  } else if (strEqual(name, "miter-limit")) {
    const double val = parse_double(value);
    if (isnan(val)) {
      goto err_parse;
    }
    cairo_set_miter_limit(cr, (val < 1.0) ? 1.0 : val);

  } else if (strEqual(name, "operator")) {
    const cairo_operator_t val = parse_operator(value);
    if (val == (cairo_operator_t)-1) {
      goto err_parse;
    }
    cairo_set_operator(cr, val);

  } else if (strEqual(name, "tolerance")) {
    const double val = parse_double(value);
    if (isnan(val)) {
      goto err_parse;
    }
    cairo_set_tolerance(cr, (val < 0.0) ? 0.0 : val);

  } else {
    WARN("attribute <set %s=...> not known", name);
    return ATTR_UNKNOWN;
  }

  return ATTR_SUCCESS;

err_parse:
  WARN("could not parse <set %s=\"%s\">", name, value);
  return ATTR_PARSE;
}
// }}}


struct _set_source_attrs_t {
  xmlcairo_surface_t *surface;
  enum {
    SSTYPE_NONE = 0,
    SSTYPE_PATTERN = 0x01,
    SSTYPE_IMAGE = 0x02,
    SSTYPE_RGB = 0x04
  } type;
  double r, g, b, a;
  cairo_pattern_t *pattern;
  cairo_surface_t *image;
  double x, y, width, height;
  enum gravity_e gravity;
};

// @r @g @b [@a]  OR  @pattern  OR  @image [@x] [@y] [@width] [@height] [@gravity]
static int set_source_attrs(const xmlChar *name, const xmlChar *value, void *user) // {{{
{
  struct _set_source_attrs_t *attrs = (struct _set_source_attrs_t *)user;

/*
  if (strEqual(name, "pattern")) {
    attrs->type |= SSTYPE_PATTERN;
    attrs->pattern = xmlHashLookup(attrs->surface->..., value);
    if (!attrs->pattern) {
      WARN("pattern \"%s\" not found", value);
      return ATTR_NOT_FOUND;
    }
    return ATTR_SUCCESS;

  } else
*/
  if (strEqual(name, "image")) {
    attrs->type |= SSTYPE_IMAGE;
    attrs->image = xmlHashLookup(attrs->surface->imgs, value);
    if (!attrs->image) {
      WARN("image \"%s\" not found", value);
      return ATTR_NOT_FOUND;
    }
    return ATTR_SUCCESS;

  } else if (strEqual(name, "gravity")) {
    attrs->type |= SSTYPE_IMAGE;
    attrs->gravity = parse_gravity(value);
    if (attrs->gravity == (enum gravity_e)-1) {
      WARN("could not parse <set-source %s=\"%s\">", name, value);
      return ATTR_PARSE;
    }
    return ATTR_SUCCESS;
  }

  const double val = parse_double(value);
  const int ret = isnan(val) ? ATTR_PARSE : ATTR_SUCCESS;

  if (strEqual(name, "x")) {
    attrs->type |= SSTYPE_IMAGE;
    attrs->x = val;
  } else if (strEqual(name, "y")) {
    attrs->type |= SSTYPE_IMAGE;
    attrs->y = val;
  } else if (strEqual(name, "width")) {
    attrs->type |= SSTYPE_IMAGE;
    attrs->width = val;
  } else if (strEqual(name, "height")) {
    attrs->type |= SSTYPE_IMAGE;
    attrs->height = val;

  } else if (strEqual(name, "r")) {
    attrs->type |= SSTYPE_RGB;
    attrs->r = val;
  } else if (strEqual(name, "g")) {
    attrs->type |= SSTYPE_RGB;
    attrs->g = val;
  } else if (strEqual(name, "b")) {
    attrs->type |= SSTYPE_RGB;
    attrs->b = val;
  } else if (strEqual(name, "a")) {
    attrs->type |= SSTYPE_RGB;
    attrs->a = val;
  } else {
    WARN("attribute <set-source %s=...> not known", name);
    return ATTR_UNKNOWN;
  }

  if (ret == ATTR_PARSE) {
    WARN("could not parse <set-source %s=\"%s\">", name, value);
  }
  return ret;
}
// }}}

struct _text_attrs_t {
  xmlcairo_surface_t *surface;
  ftfont_cairo_font_t *font;
  double size;
  double x, y;
  double max_width;

  cairo_t *cr; // for text_content
};

static int text_attrs(const xmlChar *name, const xmlChar *value, void *user) // {{{
{
  struct _text_attrs_t *attrs = (struct _text_attrs_t *)user;

  if (strEqual(name, "font")) {
    attrs->font = xmlHashLookup(attrs->surface->fonts, value);
    if (!attrs->font) {
      WARN("font \"%s\" not found", value);
      return ATTR_NOT_FOUND;
    }

  } else if (strEqual(name, "size")) {
    attrs->size = parse_double(value);
    if (isnan(attrs->size)) { // size < 0.0 will just set a negative scale matrix ...
      goto err_parse;
    }

  } else if (strEqual(name, "x")) {
    attrs->x = parse_double(value);
    if (isnan(attrs->x)) {
      goto err_parse;
    }

  } else if (strEqual(name, "y")) {
    attrs->y = parse_double(value);
    if (isnan(attrs->y)) {
      goto err_parse;
    }

  } else if (strEqual(name, "max-width")) {
    attrs->max_width = parse_double(value);
    if (isnan(attrs->max_width) || attrs->max_width < 0.0) {
      goto err_parse;
    }

  } else {
    WARN("attribute <text %s=...> not known", name);
    return ATTR_UNKNOWN;
  }

  return ATTR_SUCCESS;

err_parse:
  WARN("could not parse <text %s=\"%s\">", name, value);
  return ATTR_PARSE;
}
// }}}

static int text_content(const xmlChar *value, void *user) // {{{
{
  if (!value) {
    return ELEM_CAIRO_ERROR;  // TODO... malloc error ?
  } else if (!*value) {
    return ELEM_SUCCESS;
  }

  struct _text_attrs_t *attrs = (struct _text_attrs_t *)user;

  ftfont_cairo_set_font(attrs->cr, attrs->font, attrs->size);

  int num_glyphs;
  cairo_glyph_t *glyphs;
  if (!isnan(attrs->max_width) && attrs->max_width > 0.0) { // TODO?
    glyphs = ftfont_cairo_get_glyphs(attrs->cr, (const char *)value, -1, 0.0, 0.0, 1, 0, &num_glyphs);
    if (glyphs) {
      cairo_text_extents_t ext;
      cairo_glyph_extents(attrs->cr, glyphs, num_glyphs, &ext);

      const double scale = (ext.x_advance > attrs->max_width) ? attrs->max_width / ext.x_advance : 1.0;
      cairo_set_font_size(attrs->cr, scale * attrs->size);
      for (int i = 0; i < num_glyphs; i++) {
        glyphs[i].x = scale * glyphs[i].x + attrs->x;
        glyphs[i].y += attrs->y;
      }
    }
  } else {
    glyphs = ftfont_cairo_get_glyphs(attrs->cr, (const char *)value, -1, attrs->x, attrs->y, 1, 0, &num_glyphs);
  }
  if (!glyphs) {
    return ELEM_CAIRO_ERROR;
  }

  cairo_show_glyphs(attrs->cr, glyphs, num_glyphs);
  cairo_glyph_free(glyphs);

  return ELEM_SUCCESS;
}
// }}}


static cairo_status_t _xmlcairo_apply_list(xmlcairo_surface_t *surface, cairo_t *cr, xmlNodePtr insns);

static int _xmlcairo_apply_one(xmlcairo_surface_t *surface, cairo_t *cr, xmlNodePtr insn)
{
  if (!insn || insn->type != XML_ELEMENT_NODE) {
    return ELEM_NOT_ELEMENT;
  }
  if (!insn->name || !*insn->name) {
    WARN("empty element name");
    return ELEM_UNKNOWN; // unknown element
  }

#define CASE(c0, c1) case (((const xmlChar)(c0) << 8) + (const xmlChar)(c1))
#define EQ(full) strEqual(insn->name, full)   // +2, +2  (but probably not more efficient)
  switch ((insn->name[0] << 8) | insn->name[1]) {
  CASE('c', 'l'):
    if (EQ("clip")) {
      int preserve = 0;
      if (for_each_attr(insn, preserve_attrs, &preserve)) {
        return ELEM_BADATTR;
      }
      if (preserve) {
        cairo_clip_preserve(cr);
      } else {
        cairo_clip(cr);
      }
      return ELEM_SUCCESS;
    }
    break;

  CASE('c', 'o'):
    if (EQ("copy-page")) {
      if (for_each_attr(insn, no_attrs, NULL)) {
        return ELEM_BADATTR;
      }
      cairo_copy_page(cr);
      return ELEM_SUCCESS;
    }
    break;

  CASE('f', 'i'):
    if (EQ("fill")) {
      int preserve = 0;
      if (for_each_attr(insn, preserve_attrs, &preserve)) {
        return ELEM_BADATTR;
      }
      if (preserve) {
        cairo_fill_preserve(cr);
      } else {
        cairo_fill(cr);
      }
      return ELEM_SUCCESS;
    }
    break;

  CASE('p', 'a'):
    if (EQ("paint")) {
      double alpha = NAN;
      if (for_each_attr(insn, alpha_attrs, &alpha)) {
        return ELEM_BADATTR;
      }
      if (!isnan(alpha)) {
        cairo_paint_with_alpha(cr, alpha);
      } else {
        cairo_paint(cr);
      }
      return ELEM_SUCCESS;
    } else if (EQ("path")) {
      // TODO? ensure xmlHasProp(insn, "d"); ?  (but: default = '')
      if (for_each_attr(insn, apply_path_attrs, cr)) {
        return ELEM_BADATTR;
      }
      return ELEM_SUCCESS;
    }
    break;

  CASE('r', 'e'):
    if (EQ("reset-clip")) {
      if (for_each_attr(insn, no_attrs, NULL)) {
        return ELEM_BADATTR;
      }
      cairo_reset_clip(cr);
      return ELEM_SUCCESS;
    }
    break;

  CASE('s', 'e'):
    if (EQ("set")) {
      const int res = for_each_attr(insn, apply_set_attrs, cr);
      if (res) {
        return ELEM_BADATTR;
      }
      return ELEM_SUCCESS;

    } else if (EQ("set-source")) {
      struct _set_source_attrs_t attrs = {
        .surface = surface,
        .type = SSTYPE_NONE,
        .r = NAN, .g = NAN, .b = NAN, .a = 1.0,
        .x = 0.0, .y = 0.0, .width = NAN, .height = NAN,
        .gravity = GRAVITY_CENTER
      };
      const int res = for_each_attr(insn, set_source_attrs, &attrs);
      if (res) {
        return ELEM_BADATTR;
      }
      if ((attrs.type & (attrs.type - 1)) != 0) {
        WARN("only one of <set-source r=\"...\" g=\"...\" b=\"...\" [a=\"...\"]/>, <set-source pattern=\"...\"/>, or <set-source image=\"...\" [x=\"...\"] [y=\"...\"] [width=\"...\"] [height=\"...\"] [gravity=\"...\"]/> is allowed");
        return ELEM_BADATTR;
      }
      switch (attrs.type) {
/* FIXME
      case SSTYPE_PATTERN:
        cairo_set_source(cr, attrs.pattern);
        break;
*/
      case SSTYPE_IMAGE:
        if (!isnan(attrs.width) || !isnan(attrs.height)) {
          // assert(cairo_surface_get_type(attrs.image) == CAIRO_SURFACE_TYPE_IMAGE);
          const int ow = cairo_image_surface_get_width(attrs.image),
                    oh = cairo_image_surface_get_height(attrs.image);
          double sx, sy, dx, dy;
          compute_fit(ow, oh, attrs.width, attrs.height, attrs.gravity, &sx, &sy, &dx, &dy);

          cairo_pattern_t *pattern = cairo_pattern_create_for_surface(attrs.image);
          if (cairo_pattern_status(pattern) != CAIRO_STATUS_SUCCESS) {
            return ELEM_CAIRO_ERROR; // TODO?! error, but not on surface itself...?
          }

          cairo_matrix_t matrix;
          cairo_matrix_init(&matrix, sx, 0.0, 0.0, sy, -sx * (dx + (!isnan(attrs.x) ? attrs.x : 0.0)), -sy * (dy + (!isnan(attrs.y) ? attrs.y : 0.0)));
          cairo_pattern_set_matrix(pattern, &matrix);

          cairo_set_source(cr, pattern);
          cairo_pattern_destroy(pattern);
        } else {
          cairo_set_source_surface(cr, attrs.image, (!isnan(attrs.x) ? attrs.x : 0.0), (!isnan(attrs.y) ? attrs.y : 0.0));
        }
        break;

      case SSTYPE_RGB:
        if (isnan(attrs.r) || isnan(attrs.g) || isnan(attrs.b)) {
          WARN("all three of <set-source r=\"...\" g=\"...\" b=\"...\"/> are required");
          return ELEM_BADATTR;
        }
        cairo_set_source_rgba(cr, attrs.r, attrs.g, attrs.b, attrs.a);
        break;

      default: // no attribute -> silently ignore  // TODO?
        break;
      }
      return ELEM_SUCCESS;
    }
    break;

  CASE('s', 'h'):
    if (EQ("show-page")) {
      if (for_each_attr(insn, no_attrs, NULL)) {
        return ELEM_BADATTR;
      }
      cairo_show_page(cr);
      return ELEM_SUCCESS;
    }
    break;

  CASE('s', 't'):
    if (EQ("stroke")) {
      int preserve = 0;
      if (for_each_attr(insn, preserve_attrs, &preserve)) {
        return ELEM_BADATTR;
      }
      if (preserve) {
        cairo_stroke_preserve(cr);
      } else {
        cairo_stroke(cr);
      }
      return ELEM_SUCCESS;
    }
    break;

  CASE('s', 'u'):
    if (EQ("sub")) {
      cairo_save(cr);
      if (for_each_attr(insn, apply_transform_attrs, cr)) {
        cairo_restore(cr);
        return ELEM_BADATTR;
      }
      const cairo_status_t s = _xmlcairo_apply_list(surface, cr, insn->children);
      cairo_restore(cr);
      if (s != CAIRO_STATUS_SUCCESS) {
        return ELEM_CAIRO_ERROR; // caller will itself check cairo_status() ...
      }
      return ELEM_SUCCESS;
    }
    break;

  CASE('t', 'e'):
    if (EQ("text")) {
      struct _text_attrs_t attrs = {
        .surface = surface,
        .font = NULL,
        .size = NAN,
        .x = 0, .y = 0,
        .max_width = NAN
      };
      if (for_each_attr(insn, text_attrs, &attrs)) {
        return ELEM_BADATTR;
      }
      if (!attrs.font || isnan(attrs.size)) {
        WARN("<text font=\"...\" size=\"...\"/> are required");
        return ELEM_BADATTR;
      }

      attrs.cr = cr;
      return for_content(insn, text_content, &attrs);
    }
    break;

  default:
    break;
  }
#undef EQ
#undef CASE

  WARN("unknown element: <%s>", insn->name);
  return ELEM_UNKNOWN; // unknown element
}

static cairo_status_t _xmlcairo_apply_list(xmlcairo_surface_t *surface, cairo_t *cr, xmlNodePtr insns) // {{{
{
  cairo_status_t ret = cairo_status(cr);
  for (; insns && ret == CAIRO_STATUS_SUCCESS; insns = insns->next) {
    if (insns->type != XML_ELEMENT_NODE) {
      // TODO? error for (/allow) XML_TEXT_NODE, _CDATA_SECTION_NODE, ...?
      continue;
    }
    _xmlcairo_apply_one(surface, cr, insns);  // TODO? check error?
    ret = cairo_status(cr);
  }
  return ret;
}
// }}}


cairo_status_t xmlcairo_apply(xmlcairo_surface_t *surface, xmlNodePtr insn) // {{{
{
  if (!surface) {
    return CAIRO_STATUS_NULL_POINTER;
  }

  cairo_t *cr = cairo_create(surface->surface);
  // assert(cr);

  cairo_status_t ret = cairo_status(cr);
  if (ret == CAIRO_STATUS_SUCCESS) {
    _xmlcairo_apply_one(surface, cr, insn);  // TODO? check error?
    ret = cairo_status(cr);
  }

  cairo_destroy(cr);
  return ret;
}
// }}}

cairo_status_t xmlcairo_apply_list(xmlcairo_surface_t *surface, xmlNodePtr insns) // {{{
{
  if (!surface) {
    return CAIRO_STATUS_NULL_POINTER;
  }

  cairo_t *cr = cairo_create(surface->surface);
  // assert(cr);

  const cairo_status_t ret = _xmlcairo_apply_list(surface, cr, insns);

  cairo_destroy(cr);
  return ret;
}
// }}}

