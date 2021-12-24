#include "parse-svg-cairo.h"
#include <cairo.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>  // ?

#if __has_attribute(fallthrough)
#define FALLTHROUGH __attribute__((fallthrough))
#else
#define FALLTHROUGH
#endif

static inline const char *skipWS(const char *cur)
{
//  return cur + strspn(cur, "\x09\x0A\x0C\x0D\x20");  // SVG 2  (also in consumeCommaWS)
  return cur + strspn(cur, "\x09\x0A\x0D\x20");
}

static inline const char *consumeCommaWS(const char *cur, bool required)
{
  // /(?:${wsp}+,?|,)${wsp}*/
//  const size_t l = strspn(cur, "\x09\x0A\x0C\x0D\x20");
  const size_t l = strspn(cur, "\x09\x0A\x0D\x20");
  if (l > 0) {
    cur += l;
    if (*cur == ',') {
      cur++;
    }
  } else if (*cur == ',') {
    cur++;
  } else if (!required) {
    return cur; // (no wsp here!)
  } else {
    return NULL;
  }
  cur = skipWS(cur);

  return cur;
}

// NOTE: does not allow "2." (only allowed in SVG 1.1)
// does NOT include a sign (also cf. parseCoordinate)
static inline const char *parseNumber(const char *cur, float *ret)
{
  // /(?:[0-9]*[.])?[0-9]+(?:[eE][+-]?[0-9]+)?/
  const char *start = cur;

  cur += strspn(cur, "0123456789");
  if (*cur == '.') {
    ++cur;

    const char *tmp = cur;
    cur += strspn(cur, "0123456789");
    if (cur == tmp) {
      return NULL;
    }
  } else if (cur == start) {
    return NULL;
  }

  if (*cur == 'e' || *cur == 'E') {
    ++cur;
    if (*cur == '+') {
      ++cur;
    } else if (*cur == '-') {
      ++cur;
    }

    const char *tmp = cur;
    cur += strspn(cur, "0123456789");
    if (cur == tmp) {
      return NULL;
    }
  }

  char *str = strndup(start, cur - start), *end;
  if (!str) {
    return NULL;  // TODO?
  }
  *ret = strtof(str, &end);

  const char *r = (!*end) ? cur : NULL;
  free(str);

  return r;
}

static inline const char *parseCoordinate(const char *cur, float *ret)
{
  // /([+-]?${number})/
  if (*cur == '-') {
    cur++;
    float tmp;
    cur = parseNumber(cur, &tmp);
    if (!cur) {
      return NULL;
    }
    *ret = -tmp;
    return cur;
  }

  if (*cur == '+') {
    cur++;
  }
  return parseNumber(cur, ret);
}

static inline const char *parseCoordinateMore(const char *cur, float *ret)
{
  // /(${comma_wsp}?[+-]?${number})/
  cur = consumeCommaWS(cur, false);
  return parseCoordinate(cur, ret);
}

static inline const char *parseCoordinatePair(const char *cur, float *ret_x, float *ret_y)
{
  cur = parseCoordinate(cur, ret_x);
  if (!cur) {
    return NULL;
  }
  return parseCoordinateMore(cur, ret_y);
}

static inline const char *parseCoordinatePairMore(const char *cur, float *ret_x, float *ret_y)
{
  cur = consumeCommaWS(cur, false);
  return parseCoordinatePair(cur, ret_x, ret_y);
}

static inline const char *parseFlag(const char *cur, bool *ret_flag)
{
  if (*cur == '0') {
    *ret_flag = false;
  } else if (*cur == '1') {
    *ret_flag = true;
  } else {
    return NULL;
  }
  cur++;
  return cur;
}

static inline const char *parseArcArg(const char *cur, float *ret_rx, float *ret_ry, float *ret_phi, bool *ret_largeArc, bool *ret_sweepFlag, float *ret_x, float *ret_y)
{
  // /(${number})${comma_wsp}?(${number})${coordinate_more}${comma_wsp}([01])${comma_wsp}?([01])${coordinate_pair_more}/
  // SVG2 9.5.1. "Out-of-range elliptical arc parameters":
  // "If either rx or ry have negative signs, these are dropped; the absolute value is used instead."
  if (*cur == '+' || *cur == '-') {
    ++cur;
  }
  cur = parseNumber(cur, ret_rx);
  if (!cur) {
    return NULL;
  }
  cur = consumeCommaWS(cur, false);
  if (*cur == '+' || *cur == '-') {
    ++cur;
  }
  cur = parseNumber(cur, ret_ry);
  if (!cur) {
    return NULL;
  }
  cur = consumeCommaWS(cur, false);
  cur = parseCoordinate(cur, ret_phi);
  if (!cur) {
    return NULL;
  }
  cur = consumeCommaWS(cur, false); // not really required (but token is required), because parseFlag will finally error out
  cur = parseFlag(cur, ret_largeArc);
  if (!cur) {
    return NULL;
  }
  cur = consumeCommaWS(cur, false);
  cur = parseFlag(cur, ret_sweepFlag);
  if (!cur) {
    return NULL;
  }
  cur = parseCoordinatePairMore(cur, ret_x, ret_y);
  if (!cur) {
    return NULL;
  }
  return cur;
}

static inline const char *parseArcArgMore(const char *cur, float *ret_rx, float *ret_ry, float *ret_phi, bool *ret_largeArc, bool *ret_sweepFlag, float *ret_x, float *ret_y) {
  cur = consumeCommaWS(cur, false);
  return parseArcArg(cur, ret_rx, ret_ry, ret_phi, ret_largeArc, ret_sweepFlag, ret_x, ret_y);
}

// ---

#include <math.h>

static inline float sqr(float val)
{
  return val * val;
}

static const double pi = 3.14159265358979323846;

#define EPS 10e-6

struct _path_builder_t {
  void (*move_to)(void *user, float x, float y);
  void (*line_to)(void *user, float x, float y);
  void (*quad_to)(void *user, float cx0, float cy0, float x, float y);
  void (*curve_to)(void *user, float cx0, float cy0, float cx1, float cy1, float x, float y);
  void (*arc_to)(void *user, float cx, float cy, float rx, float ry, float phi, float start, float delta);
  void (*close)(void *user);
};

struct _psp_state {
  const struct _path_builder_t *builder;
  void *user;

  float cx, cy, tx, ty;
  enum { NONE, QUAD, CURVE } last;
};

static void psp_move_to(struct _psp_state *state, float x, float y, bool rel)
{
  if (rel) {
    x += state->cx;
    y += state->cy;
  }
  state->builder->move_to(state->user, x, y);
  state->cx = x;
  state->cy = y;
  state->last = NONE;
}

static void psp_line_to(struct _psp_state *state, float x, float y, bool rel)
{
  if (rel) {
    x += state->cx;
    y += state->cy;
  }
  state->builder->line_to(state->user, x, y);
  state->cx = x;
  state->cy = y;
  state->last = NONE;
}

static void psp_hline_to(struct _psp_state *state, float x, bool rel)
{
  if (rel) {
    x += state->cx;
  }
  state->builder->line_to(state->user, x, state->cy);
  state->cx = x;
  state->last = NONE;
}

static void psp_vline_to(struct _psp_state *state, float y, bool rel)
{
  if (rel) {
    y += state->cy;
  }
  state->builder->line_to(state->user, state->cx, y);
  state->cy = y;
  state->last = NONE;
}

static void psp_quad_to(struct _psp_state *state, float cx0, float cy0, float x, float y, bool rel)
{
  if (rel) {
    cx0 += state->cx;
    cy0 += state->cy;
    x += state->cx;
    y += state->cy;
  }
  state->builder->quad_to(state->user, cx0, cy0, x, y);
  state->tx = 2 * x - cx0;
  state->ty = 2 * y - cy0;
  state->cx = x;
  state->cy = y;
  state->last = QUAD;
}

static void psp_smooth_quad_to(struct _psp_state *state, float x, float y, bool rel)
{
  if (rel) {
    x += state->cx;
    y += state->cy;
  }
  if (state->last != QUAD) {
    state->last = QUAD;
    state->builder->line_to(state->user, x, y);
    state->tx = state->cx;
    state->ty = state->cy;
  } else {
    state->builder->quad_to(state->user, state->tx, state->ty, x, y);
    state->tx = 2 * x - state->tx;
    state->ty = 2 * y - state->ty;
  }
  state->cx = x;
  state->cy = y;
}

static void psp_curve_to(struct _psp_state *state, float cx0, float cy0, float cx1, float cy1, float x, float y, bool rel)
{
  if (rel) {
    cx0 += state->cx;
    cy0 += state->cy;
    cx1 += state->cx;
    cy1 += state->cy;
    x += state->cx;
    y += state->cy;
  }
  state->builder->curve_to(state->user, cx0, cy0, cx1, cy1, x, y);
  state->tx = 2 * x - cx0;
  state->ty = 2 * y - cy0;
  state->cx = x;
  state->cy = y;
  state->last = CURVE;
}

static void psp_smooth_curve_to(struct _psp_state *state, float cx1, float cy1, float x, float y, bool rel)
{
  if (rel) {
    cx1 += state->cx;
    cy1 += state->cy;
    x += state->cx;
    y += state->cy;
  }
  if (state->last != CURVE) {
    state->last = CURVE;
    state->builder->quad_to(state->user, cx1, cy1, x, y);
  } else {
    state->builder->curve_to(state->user, state->tx, state->ty, cx1, cy1, x, y);
  }
  state->tx = 2 * x - cx1;
  state->ty = 2 * y - cy1;
  state->cx = x;
  state->cy = y;
}

// sweepFlag = true: positive angle
static void psp_arc_to(struct _psp_state *state, float rx, float ry, float phi, bool largeArc, bool sweepFlag, float x, float y, bool rel)
{
  rx = fabsf(rx);
  ry = fabsf(ry);
  if (rx < EPS || ry < EPS) {
    psp_line_to(state, x, y, rel);
    return;
  }

  float odx, ody;
  if (rel) {
    // do not add cx/cy just yet
    odx = -0.5f * x;
    ody = -0.5f * y;
  } else {
    odx = 0.5f * (state->cx - x);
    ody = 0.5f * (state->cy - y);
  }

  const float c = cosf(phi), s = sinf(phi);
  const float dx = odx * c + ody * s,
              dy = -odx * s + ody * c;

  // cf. https://www.w3.org/TR/SVG/implnote.html#ArcCorrectionOutOfRangeRadii
  float rt;
  const float lambda = sqr(dx / rx) + sqr(dy / ry);
  if (lambda <= 1) {
    const float rxdy2 = sqr(rx * dy),
                rydx2 = sqr(ry * dx);
    rt = sqrtf((sqr(rx * ry) - rxdy2 - rydx2) / (rxdy2 + rydx2));
    if (largeArc == sweepFlag) {
      rt = -rt;
    }
  } else { // must enlarge radii to span  cur -- (x, y)
    rt = 0; // (for rx *= sqrt(lambda); ry *= sqrt(lambda); everything cancels)
  }

  const float cpx = rt * rx * dy / ry,
              cpy = -rt * ry * dx / rx;

  const float p0x = (dx - cpx) / rx,
              p0y = (dy - cpy) / ry;
  const float p1x = (-dx - cpx) / rx,
              p1y = (-dy - cpy) / ry;
  const float start = atan2f(p0y, p0x);
  float delta = atan2f(p0x * p1y - p0y * p1x, p0x * p1x + p0y * p1y); // (p0 cross p1, p0 dot p1)

  if (sweepFlag) {
    if (delta > 0) {
      delta -= 2 * pi;
    }
  } else if (delta < 0) {
    delta += 2 * pi;
  }

  if (rel) {
    x += state->cx;
    y += state->cy;
  }
  const float centerx = cpx * c - cpy * s + 0.5f * (state->cx + x),
              centery = cpx * s + cpy * c + 0.5f * (state->cy + y);
  state->builder->arc_to(state->user, centerx, centery, rx, ry, phi, start, delta);

  state->cx = x;
  state->cy = y;
  state->last = NONE;
}

static void psp_close(struct _psp_state *state)
{
  state->builder->close(state->user);
  state->last = NONE;
}

// returns < 0 on success, otherwise: position of first problematic character
int parse_svg_path(const char *path, const struct _path_builder_t *builder, void *user)
{
  if (!path) {
    return 0;  // todo?
  }
  const char *cur = path, *tmp;

  cur = skipWS(cur);
  if (!*cur) {
    return -1;
  } else if (*cur != 'M' && *cur != 'm') { // first cmd must be a move!
    return cur - path;
  }

  bool rel;
  float x, y, cx0, cy0, cx1, cy1;
  struct _psp_state psp = {
    builder, user,
    0, 0,  0, 0,
    NONE
  };
  while (*cur) {
    rel = true;
    switch (*cur) {
    case 'M':
      rel = false;
      FALLTHROUGH;
    case 'm':
      cur = skipWS(++cur);
      tmp = parseCoordinatePair(cur, &x, &y);
      if (!tmp) {
        return cur - path;
      }
      psp_move_to(&psp, x, y, rel);
      while ((tmp = parseCoordinatePairMore(cur = tmp, &x, &y))) {
        psp_line_to(&psp, x, y, rel);
      }
      break;

    case 'Z':
      rel = false;
      FALLTHROUGH;
    case 'z':
      ++cur;
      psp_close(&psp);
      break;

    case 'L':
      rel = false;
      FALLTHROUGH;
    case 'l':
      cur = skipWS(++cur);
      tmp = parseCoordinatePair(cur, &x, &y);
      if (!tmp) {
        return cur - path;
      }
      psp_line_to(&psp, x, y, rel);
      while ((tmp = parseCoordinatePairMore(cur = tmp, &x, &y))) {
        psp_line_to(&psp, x, y, rel);
      }
      break;

    case 'H':
      rel = false;
      FALLTHROUGH;
    case 'h':
      cur = skipWS(++cur);
      tmp = parseCoordinate(cur, &x);
      if (!tmp) {
        return cur - path;
      }
      psp_hline_to(&psp, x, rel);
      while ((tmp = parseCoordinateMore(cur = tmp, &x))) {
        psp_hline_to(&psp, x, rel);
      }
      break;

    case 'V':
      rel = false;
      FALLTHROUGH;
    case 'v':
      cur = skipWS(++cur);
      tmp = parseCoordinate(cur, &y);
      if (!tmp) {
        return cur - path;
      }
      psp_vline_to(&psp, y, rel);
      while ((tmp = parseCoordinateMore(cur = tmp, &y))) {
        psp_vline_to(&psp, y, rel);
      }
      break;

    case 'C':
      rel = false;
      FALLTHROUGH;
    case 'c':
      cur = skipWS(++cur);
      if (!(tmp = parseCoordinatePair(cur, &cx0, &cy0)) ||
          !(tmp = parseCoordinatePairMore(tmp, &cx1, &cy1)) ||
          !(tmp = parseCoordinatePairMore(tmp, &x, &y))) {
        return cur - path;
      }
      psp_curve_to(&psp, cx0, cy0, cx1, cy1, x, y, rel);
      while ((tmp = parseCoordinatePairMore(cur = tmp, &cx0, &cy0)) &&
             (tmp = parseCoordinatePairMore(tmp, &cx1, &cy1)) &&
             (tmp = parseCoordinatePairMore(tmp, &x, &y))) {
        psp_curve_to(&psp, cx0, cy0, cx1, cy1, x, y, rel);
      }
      break;

    case 'S':
      rel = false;
      FALLTHROUGH;
    case 's':
      cur = skipWS(++cur);
      if (!(tmp = parseCoordinatePair(cur, &cx1, &cy1)) ||
          !(tmp = parseCoordinatePairMore(tmp, &x, &y))) {
        return cur - path;
      }
      psp_smooth_curve_to(&psp, cx1, cy1, x, y, rel);
      while ((tmp = parseCoordinatePairMore(cur = tmp, &cx1, &cy1)) &&
             (tmp = parseCoordinatePairMore(tmp, &x, &y))) {
        psp_smooth_curve_to(&psp, cx1, cy1, x, y, rel);
      }
      break;

    case 'Q':
      rel = false;
      FALLTHROUGH;
    case 'q':
      cur = skipWS(++cur);
      if (!(tmp = parseCoordinatePair(cur, &cx0, &cy0)) ||
          !(tmp = parseCoordinatePairMore(tmp, &x, &y))) {
        return cur - path;
      }
      psp_quad_to(&psp, cx0, cy0, x, y, rel);
      while ((tmp = parseCoordinatePairMore(cur = tmp, &cx0, &cy0)) &&
             (tmp = parseCoordinatePairMore(tmp, &x, &y))) {
        psp_quad_to(&psp, cx0, cy0, x, y, rel);
      }
      break;

    case 'T':
      rel = false;
      FALLTHROUGH;
    case 't':
      cur = skipWS(++cur);
      tmp = parseCoordinatePair(cur, &x, &y);
      if (!tmp) {
        return cur - path;
      }
      psp_smooth_quad_to(&psp, x, y, rel);
      while ((tmp = parseCoordinatePairMore(cur = tmp, &x, &y))) {
        psp_smooth_quad_to(&psp, x, y, rel);
      }
      break;

    case 'A':
      rel = false;
      FALLTHROUGH;
    case 'a': {
      float rx, ry, phi;
      bool largeArc, sweepFlag;

      cur = skipWS(++cur);
      tmp = parseArcArg(cur, &rx, &ry, &phi, &largeArc, &sweepFlag, &x, &y);
      if (!tmp) {
        return cur - path;
      }
      psp_arc_to(&psp, rx, ry, phi * pi / 180.0, largeArc, sweepFlag, x, y, rel);
      while ((tmp = parseArcArgMore(cur = tmp, &rx, &ry, &phi, &largeArc, &sweepFlag, &x, &y))) {
        psp_arc_to(&psp, rx, ry, phi * pi / 180.0, largeArc, sweepFlag, x, y, rel);
      }
      break;
    }

    default:
      return cur - path;
    }

    cur = skipWS(cur);
  }

  return -1;
}


static void _pbcairo_move_to(void *user, float x, float y)
{
  cairo_move_to((cairo_t *)user, x, y);
}

static void _pbcairo_line_to(void *user, float x, float y)
{
  cairo_line_to((cairo_t *)user, x, y);
}

static void _pbcairo_quad_to(void *user, float cx0, float cy0, float x, float y)
{
  cairo_curve_to((cairo_t *)user, cx0, cy0, x, y, x, y);
}

static void _pbcairo_curve_to(void *user, float cx0, float cy0, float cx1, float cy1, float x, float y)
{
  cairo_curve_to((cairo_t *)user, cx0, cy0, cx1, cy1, x, y);
}

static void _pbcairo_arc_to(void *user, float cx, float cy, float rx, float ry, float phi, float start, float delta)
{
  cairo_t *cr = (cairo_t *)user;
  cairo_save(cr);
  cairo_translate(cr, cx, cy);
  cairo_rotate(cr, phi);
  cairo_scale(cr, rx, ry);
  cairo_arc(cr, 0.0, 0.0, 1.0, start, start + delta);
  cairo_restore(cr);
}

static void _pbcairo_close(void *user)
{
  cairo_close_path((cairo_t *)user);
}

static const struct _path_builder_t pbcairo = {
  &_pbcairo_move_to,
  &_pbcairo_line_to,
  &_pbcairo_quad_to,
  &_pbcairo_curve_to,
  &_pbcairo_arc_to,
  &_pbcairo_close
};

int apply_svg_cairo_path(cairo_t *cr, const char *path)
{
  // cairo_new_sub_path(cr);  // not needed, svg always starts with M/m
  cairo_new_path(cr);  // TODO?
  return parse_svg_path(path, &pbcairo, cr);
}

// ---

// https://www.w3.org/TR/css-transforms-1/#svg-syntax
// TODO? "The syntax reflects implemented behavior in user agents and differs from the syntax defined by SVG 1.1."

// https://www.w3.org/TR/2011/REC-SVG11-20110816/coords.html#TransformAttribute

struct _transform_builder_t {
  void (*matrix)(void *user, float a, float b, float c, float d, float e, float f);
  void (*translate)(void *user, float x, float y);
  void (*scale)(void *user, float x, float y);
  void (*rotate)(void *user, float a, float x, float y);
  void (*skewX)(void *user, float ax);
  void (*skewY)(void *user, float ay);
};

static inline const char *parseArgMore(const char *cur, float *ret)
{
  cur = consumeCommaWS(cur, true);
  if (!cur) {
    return NULL;
  }
  return parseCoordinate(cur, ret);
}

int parse_svg_transform(const char *str, const struct _transform_builder_t *builder, void *user)
{
  if (!str) {
    return 0;  // todo?
  }

  const char *cur = str, *tmp;

  cur = skipWS(cur);
  while (*cur) {
    switch (*cur) {
    // NOTE/TODO? 1.1 spec also allows " , , " ... (browser: chrome does not allow, firefox does ...)
    // case ',': break;  // NOTE: do not eat ',', but leave it for consumeCommaWS(,true)

    case 'm':
      if (strncmp(cur + 1, "atrix", 5) == 0) {
        float a, b, c, d, e, f;
        cur = skipWS(cur += 6);
        if (*cur != '(' ||
            !(tmp = parseCoordinate(++cur, &a)) ||
            !(tmp = parseArgMore(tmp, &b)) ||
            !(tmp = parseArgMore(tmp, &c)) ||
            !(tmp = parseArgMore(tmp, &d)) ||
            !(tmp = parseArgMore(tmp, &e)) ||
            !(tmp = parseArgMore(tmp, &f)) ||
            *(cur = skipWS(tmp)) != ')') {
          return cur - str;
        }
        ++cur;
        builder->matrix(user, a, b, c, d, e, f);
      } else {
        return cur - str;
      }
      break;

    case 'r':
      if (strncmp(cur + 1, "otate", 5) == 0) {
        float a, x, y;
        cur = skipWS(cur += 6);
        if (*cur != '(' ||
            !(tmp = parseCoordinate(++cur, &a))) {
          return cur - str;
        }
        if (!(tmp = parseArgMore(cur = tmp, &x)) ||
            !(tmp = parseArgMore(tmp, &y))) {
          x = y = 0;
        } else {
          cur = tmp;
        }
        cur = skipWS(cur);
        if (*cur != ')') {
          return cur - str;
        }
        ++cur;
        builder->rotate(user, a * pi / 180.0, x, y);
      } else {
        return cur - str;
      }
      break;

    case 's':
      if (strncmp(cur + 1, "cale", 4) == 0) {
        float x, y;
        cur = skipWS(cur += 5);
        if (*cur != '(' ||
            !(tmp = parseCoordinate(++cur, &x))) {
          return cur - str;
        }
        if (!(tmp = parseArgMore(cur = tmp, &y))) {
          y = x;
        } else {
          cur = tmp;
        }
        cur = skipWS(cur);
        if (*cur != ')') {
          return cur - str;
        }
        ++cur;
        builder->scale(user, x, y);

      } else if (strncmp(cur + 1, "kew", 3) == 0 &&
                 (cur[4] == 'X' || cur[4] == 'Y')) {
        const char type = cur[4];
        float a;
        cur = skipWS(cur += 5);
        if (*cur != '(' ||
            !(tmp = parseCoordinate(++cur, &a)) ||
            *(cur = skipWS(tmp)) != ')') {
          return cur - str;
        }
        ++cur;
        if (type == 'X') {
          builder->skewX(user, a);
        } else {
          builder->skewY(user, a);
        }

      } else {
        return cur - str;
      }
      break;

    case 't':
      if (strncmp(cur + 1, "ranslate", 8) == 0) {
        float x, y;
        cur = skipWS(cur += 9);
        if (*cur != '(' ||
            !(tmp = parseCoordinate(++cur, &x))) {
          return cur - str;
        }
        if (!(tmp = parseArgMore(cur = tmp, &y))) {
          y = 0;
        } else {
          cur = tmp;
        }
        cur = skipWS(cur);
        if (*cur != ')') {
          return cur - str;
        }
        ++cur;
        builder->translate(user, x, y);
      } else {
        return cur - str;
      }
      break;

    default:
      return cur - str;
    }

    cur = consumeCommaWS(tmp = cur, false);
    if (cur == tmp) {
      // return tmp - str;  // NOTE(/FIXME?):  browsers do allow "translate(1)translate(2)" w/o comma or WS ...
      continue;
    } else if (!*cur) { // trailing comma is not allowed
      return tmp - str;
    }
  }

  return -1;
}


static void _tbcairo_matrix(void *user, float a, float b, float c, float d, float e, float f)
{
  cairo_matrix_t m;
  cairo_matrix_init(&m, a, b, c, d, e, f);
  cairo_matrix_multiply((cairo_matrix_t *)user, (cairo_matrix_t *)user, &m);
}

static void _tbcairo_translate(void *user, float x, float y)
{
  cairo_matrix_translate((cairo_matrix_t *)user, x, y);
}

static void _tbcairo_scale(void *user, float x, float y)
{
  cairo_matrix_scale((cairo_matrix_t *)user, x, y);
}

static void _tbcairo_rotate(void *user, float a, float x, float y)
{
  if (x == 0.0f && y == 0.0f) {
    cairo_matrix_rotate((cairo_matrix_t *)user, a);
  } else {
    cairo_matrix_translate((cairo_matrix_t *)user, x, y);
    cairo_matrix_rotate((cairo_matrix_t *)user, a);
    cairo_matrix_translate((cairo_matrix_t *)user, -x, -y);
  }
}

static void _tbcairo_skewX(void *user, float ax)
{
  cairo_matrix_t m;
  cairo_matrix_init(&m, 1.0, 0.0, tanf(ax), 1.0, 0.0, 0.0);
  cairo_matrix_multiply((cairo_matrix_t *)user, (cairo_matrix_t *)user, &m);
}

static void _tbcairo_skewY(void *user, float ay)
{
  cairo_matrix_t m;
  cairo_matrix_init(&m, 1.0, tanf(ay), 0.0, 1.0, 0.0, 0.0);
  cairo_matrix_multiply((cairo_matrix_t *)user, (cairo_matrix_t *)user, &m);
}

static const struct _transform_builder_t tbcairo = {
  &_tbcairo_matrix,
  &_tbcairo_translate,
  &_tbcairo_scale,
  &_tbcairo_rotate,
  &_tbcairo_skewX,
  &_tbcairo_skewY
};

int parse_svg_cairo_transform(cairo_matrix_t *matrix, const char *str)
{
  return parse_svg_transform(str, &tbcairo, matrix);
}


// NOTE: both FF and Chrome allow leading WSP (but not COMMA)
// NOTE: does not support length units or percentages
int parse_svg_cairo_dasharray(struct cairo_svg_dasharray_s *ret, const char *str)
{
  if (!ret) {
    return 0; // TODO?
  } else if (!str) {
    return 0; // todo?
  }

  if (ret->dashes) {
    free_dasharray(ret); // TODO?
  }

  const char *cur = str, *tmp;

  // estimate number of elements for malloc
  ret->num_dashes = 0;
  cur = skipWS(cur);
  while (*cur) {
    tmp = cur + strspn(cur, "0123456789+-.eE");
    if (cur == tmp) {
      return cur - str;
    }
    cur = consumeCommaWS(tmp, false);
    ret->num_dashes++;
  }

  if (!ret->num_dashes) {
    return -1;
  }

  ret->dashes = malloc(ret->num_dashes * sizeof(*ret->dashes));
  if (!ret->dashes) {
    return 0; // TODO?!
  }

  double *dash = ret->dashes;
  cur = skipWS(str);
  while (*cur) {
    float val;
    tmp = parseCoordinate(cur, &val);
    if (!tmp || val < 0.0f) {
      free_dasharray(ret);
      return cur - str;
    }
    cur = consumeCommaWS(tmp, false);
    *dash++ = val;
  }

  return -1;
}

void free_dasharray(struct cairo_svg_dasharray_s *da)
{
  if (da) {
    free(da->dashes); // (accepts NULL)
    da->dashes = NULL;
  }
}

