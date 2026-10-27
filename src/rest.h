#ifndef REST_H
#define REST_H

/* Rest mode. While the console wakes up it must be left alone: a call into SceShellUI and a new
 * virtual pad at that moment left it on a grey screen or a system software error (PS5, 27 Oct).
 * Any thread may signal a rest transition; until the quiet period ends, OmniPad injects nothing,
 * adds and removes no virtual pad and calls nothing in the system. Then every controller is
 * released, as the console does with its own pads. */

/* A sign of entering or leaving rest mode: starts or extends the quiet period */
void rest_signal(const char *why);

/* 1 while OmniPad must leave the system alone */
int rest_quiet(void);

/* 1 once, when a quiet period has ended: time to release every controller */
int rest_take_resume(void);

#endif /* REST_H */
