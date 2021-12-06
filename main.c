#include "xmlcairo.h"
#include <stdio.h>
#include <cairo.h>
#include <libxml/tree.h>

int main()
{
  // getopt()...

  xmlDocPtr doc = xmlParseFile("in.xml");
  if (!doc) {
    fprintf(stderr, "parsing in.xml failed\n");  // TODO? error info?
    return 1;
  }
  xmlNodePtr node = xmlDocGetRootElement(doc);
printf("root: %s\n", node->name); // TODO?! expect <surface> ?

  // TODO:  create xmlcairo_surface_t *sfc = ... from_surface_attrs ...
  // IDEA: pre-extract  certain elements: font-loading, image-loading, ...

  xmlcairo_surface_t *sfc = xmlcairo_surface_create_png("out.png", CAIRO_FORMAT_RGB24, 100, 100);

  // TODO: load image/font via xml
  if (xmlcairo_load_image(sfc, "tex0", "tex0.png") != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr, "failed to read tex0.png\n");
//   return 1;  // TODO? (+ xmlcairo_surface_destroy ??)
  }
  if (xmlcairo_load_font(sfc, "font0", "MarkOT-Black.otf") != CAIRO_STATUS_SUCCESS) {
    fprintf(stderr, "failed to load font0\n");
  }

  cairo_status_t st = xmlcairo_apply_list(sfc, node->children);
  printf("status: %d\n", st);

  st = xmlcairo_surface_destroy(sfc);

  xmlFreeDoc(doc);
  if (st != CAIRO_STATUS_SUCCESS) {
    return 1;
  }

  return 0;
}

