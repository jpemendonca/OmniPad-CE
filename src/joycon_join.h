#ifndef JOYCON_JOIN_H
#define JOYCON_JOIN_H

#include "bt/pad.h"

#include <stdint.h>

/* The model id of two joined Joy-Cons, so that Map buttons has one place for their rules. No real
 * device reports it. */
#define JOYCON_PAIR_PID 0x2008

typedef enum { JOYCON_NONE, JOYCON_LEFT, JOYCON_RIGHT } joycon_side_t;

typedef struct {
    joycon_side_t side;
    int       mate;         /* host slot of the other half, -1: alone */
    pad_state last;         /* its latest report, as the profile reads it: held sideways */
} joycon_t;

/* Call when a pad connects: which side it is, if it is a Joy-Con, and alone. */
void joycon_start(joycon_t *j, uint16_t vid, uint16_t pid);

/* 1 if a and b are a left and a right Joy-Con, both alone. */
int joycon_can_pair(const joycon_t *a, const joycon_t *b);

/* The one controller two Joy-Cons make, from their reports as joycon_t.last holds them. */
void joycon_merge(pad_state *out, const pad_state *left, const pad_state *right);

#endif /* JOYCON_JOIN_H */
