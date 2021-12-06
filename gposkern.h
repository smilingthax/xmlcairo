#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _gpos_pair_lookup_t gpos_pair_lookup_t;

// script / language can be NULL
// returns NULL when not parseable/valid/supported
gpos_pair_lookup_t *gpos_pair_lookup_create(const unsigned char *buf, size_t len, const char *script, const char *language);

int gpos_pair_lookup_get(const gpos_pair_lookup_t *gpl, unsigned short firstGID, unsigned short secondGID);

void gpos_pair_lookup_destroy(gpos_pair_lookup_t *gpl);

#ifdef __cplusplus
};
#endif
