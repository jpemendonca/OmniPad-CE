#ifndef PS_GATE_H
#define PS_GATE_H

/* How often a PS press may reach SceShellUI. Each press attaches to that process with ptrace.
 * PS presses 20 to 76 ms apart came right before it refused the attach and restarted (PS5, 3 Nov). */

#define PS_GATE_MS      500     /* a press this soon after the last one is the same press */
#define PS_GATE_FAIL_MS 2000    /* after a refused attach, leave the process alone this long */

typedef struct {
    long block_until;
} ps_gate_t;

/* Returns 1 if this press may go through. A press that is held back also extends the wait, so a
 * burst counts as one press however long it lasts. */
static inline int ps_gate_allow(ps_gate_t *g, long now)
{
    int allowed = now >= g->block_until;
    if (now + PS_GATE_MS > g->block_until) g->block_until = now + PS_GATE_MS;
    return allowed;
}

/* Call when the press could not be delivered. */
static inline void ps_gate_failed(ps_gate_t *g, long now)
{
    g->block_until = now + PS_GATE_FAIL_MS;
}

#endif /* PS_GATE_H */
