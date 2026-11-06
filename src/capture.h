#ifndef CAPTURE_H
#define CAPTURE_H

#include <stdint.h>

/* Records the raw input reports of one slot, with what OmniPad made of each, so host tests can
 * replay them (tests/test_captures.c). Off until the API starts it: with it off, each report costs
 * one flag check. Only tools/record_pads.py uses it; the dashboard has no control for it. */

#define CAPTURE_KIND_USB 'U'
#define CAPTURE_KIND_BT  'B'

/* Starts recording `slot` for up to `seconds` (1..300), dropping any earlier recording.
 * Returns 0 if the buffer could not be allocated. */
int  capture_start(int slot, int seconds);

/* Ends the recording now. What was recorded stays readable. */
void capture_stop(void);

/* True while a recording runs for this slot. */
int  capture_wants(int slot);

/* Adds a step label, such as "press cross", to the recording. */
void capture_mark(const char *label);

/* Adds one report and the state its parser gave (result: the parser's return value). */
void capture_add(char kind, int slot, uint16_t vid, uint16_t pid, const uint8_t *rep, int len, int result,
                 uint32_t buttons, uint8_t lx, uint8_t ly, uint8_t rx, uint8_t ry, uint8_t l2, uint8_t r2);

/* Writes entries from index `since` as text lines into out, and returns the length. *next is
 * the index to ask for next, *total the entries so far, *state 0 off, 1 recording, 2 finished,
 * 3 stopped because the buffer filled. */
int  capture_read(int since, char *out, int n, int *next, int *total, int *state);

#endif /* CAPTURE_H */
