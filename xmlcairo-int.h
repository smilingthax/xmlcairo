#pragma once

typedef struct _cairo_surface cairo_surface_t;

typedef struct _xmlOutputBuffer *xmlOutputBufferPtr;
typedef struct _xmlHashTable *xmlHashTablePtr;

typedef struct _ftfont_cairo_mgr ftfont_cairo_mgr_t;

struct _xmlcairo_surface_t {
  cairo_surface_t *surface;

  xmlOutputBufferPtr obuf;

  xmlHashTablePtr imgs;

  ftfont_cairo_mgr_t *fmgr;
  xmlHashTablePtr fontfiles;
  xmlHashTablePtr fonts;
};

