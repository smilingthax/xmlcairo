#pragma once

// ... #include <cairo.h>
typedef enum _cairo_status cairo_status_t;
typedef enum _cairo_content cairo_content_t;
typedef enum _cairo_format cairo_format_t;

// ... #include <libxml/tree.h>
typedef struct _xmlNode xmlNode;
typedef xmlNode *xmlNodePtr;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _xmlcairo_surface_t xmlcairo_surface_t;

xmlcairo_surface_t *xmlcairo_surface_create_pdf(const char *filename, double width_in_points, double height_in_points);
xmlcairo_surface_t *xmlcairo_surface_create_png(const char *filename, cairo_format_t format, int width, int height);
xmlcairo_surface_t *xmlcairo_surface_create_ps(const char *filename, double width_in_points, double height_in_points);
xmlcairo_surface_t *xmlcairo_surface_create_svg(const char *filename, double width_in_points, double height_in_points);
xmlcairo_surface_t *xmlcairo_surface_create_script(const char *filename, cairo_content_t content, double width, double height);

cairo_status_t xmlcairo_load_image(xmlcairo_surface_t *surface, const char *key, const char *filename);
cairo_status_t xmlcairo_load_font(xmlcairo_surface_t *surface, const char *key, const char *filename);

cairo_status_t xmlcairo_apply(xmlcairo_surface_t *surface, xmlNodePtr insn);
cairo_status_t xmlcairo_apply_list(xmlcairo_surface_t *surface, xmlNodePtr insns);

cairo_status_t xmlcairo_surface_destroy(xmlcairo_surface_t *surface);

#ifdef __cplusplus
};
#endif

