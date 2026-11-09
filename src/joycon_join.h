#ifndef JOYCON_JOIN_H
#define JOYCON_JOIN_H

#include "bt/pad.h"

#include <stdint.h>

/* The model id of two joined Joy-Cons, so that Map buttons keeps their rules apart from a single
 * Joy-Con's. No real device reports it. */
#define JOYCON_PAIR_PID 0x2008

typedef enum { JOYCON_NONE, JOYCON_LEFT, JOYCON_RIGHT } joycon_side_t;

typedef struct {
    joycon_side_t side;
    int       mate;         /* host slot of the other half, -1: alone */
    int       waiting;      /* connected with no slot of its own, in case it joins */
    long      linked_at;    /* when it connected */
    long      held_since;   /* when its L or R went down; 0: up */
    pad_state last;         /* its latest report, as the profile reads it: held sideways */
} joycon_t;

/* Call when a pad connects: which side it is, if it is a Joy-Con, and alone. */
void joycon_start(joycon_t *j, uint16_t vid, uint16_t pid, long now);

/* Call with every report of a Joy-Con. */
void joycon_track(joycon_t *j, const pad_state *st, long now);

/* 1 if a and b are a left and a right Joy-Con, both alone. */
int joycon_can_pair(const joycon_t *a, const joycon_t *b);

/* 1 if a and b can pair, both hold their L or R long enough, and the newer one connected recently. */
int joycon_can_join(const joycon_t *a, const joycon_t *b, long now);

/* 1 if a Joy-Con that waits to join should take a slot of its own: its time ran out, or it pressed
 * a button other than the shoulders, so someone is playing with it. */
int joycon_wait_over(const joycon_t *j, long now);

/* 1 if a keeps its slot when joining b, 0 if b keeps it. */
int joycon_first_keeps_slot(const joycon_t *a, const joycon_t *b);

/* The one controller two Joy-Cons make, from their reports as joycon_t.last holds them. */
void joycon_merge(pad_state *out, const pad_state *left, const pad_state *right);

#endif /* JOYCON_JOIN_H */
