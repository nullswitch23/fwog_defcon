#ifndef VP_ART_H
#define VP_ART_H
#include "vp_proto.h"

typedef struct {
    const char *name;
    const char *slot;
    const char *bonus;
    const char *lore;
} vp_art_t;

extern const vp_art_t vp_arts[VP_NART];

const vp_art_t *vp_art_get(unsigned id);

#endif
