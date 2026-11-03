#ifndef REMAP_H
#define REMAP_H

#include "pad_types.h"

/* Button remapping, per controller model. With no rules for a model its state passes untouched,
 * so a user who configures nothing runs the same code path as before. */

#define REMAP_MAX_PADS  16
#define REMAP_MAX_RULES 8

/* Loads the rules from path and remembers it for saving. A missing file means no rules. */
void remap_init(const char *path);

/* True once any model has rules. Call before remap_apply, so the state is only copied when needed. */
int  remap_active(void);

/* Applies the rules of st's vid:pid to its buttons and triggers. */
void remap_apply(pad_state_t *st);

/* Sets a model's rules, as "l3+r3=touchpad,create=square", and saves them; "" clears them.
 * Returns 0 and changes nothing if the text is not understood or there is no room. */
int  remap_set(uint16_t vid, uint16_t pid, const char *rules);

/* Writes every model's rules as {"pads":[{"vid":1,"pid":2,"rules":"..."}]}. Returns the length. */
int  remap_json(char *out, int n);

#endif /* REMAP_H */
