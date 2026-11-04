/* The limit on PS presses sent to SceShellUI (src/ps_gate.h). Times are the ones in the PS5 logs. */
#include "ps_gate.h"

#include <stdio.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void test_ps_gate_allow_firstPress_goesThrough(void)
{
    /* arrange */
    ps_gate_t g = { 0 };

    /* act */
    int allowed = ps_gate_allow(&g, 1000);

    /* assert */
    CHECK(allowed, "the first press reaches the console");
}

static void test_ps_gate_allow_burst_countsAsOnePress(void)
{
    /* arrange: the presses of 3 Nov, 23:18:54 */
    static const long times[] = { 232, 312, 380, 448 };
    ps_gate_t g = { 0 };
    int allowed = 0;

    /* act */
    for (size_t i = 0; i < sizeof times / sizeof times[0]; i++) allowed += ps_gate_allow(&g, 1000 + times[i]);

    /* assert */
    CHECK(allowed == 1, "one press out of the burst, got %d", allowed);
}

static void test_ps_gate_allow_longBurst_staysBlocked(void)
{
    /* arrange */
    ps_gate_t g = { 0 };
    int allowed = 0;

    /* act: a press every 100 ms for 3 s */
    for (long t = 0; t < 3000; t += 100) allowed += ps_gate_allow(&g, 1000 + t);

    /* assert */
    CHECK(allowed == 1, "the wait grows with the burst, got %d presses", allowed);
}

static void test_ps_gate_allow_afterAPause_goesThrough(void)
{
    /* arrange */
    ps_gate_t g = { 0 };
    ps_gate_allow(&g, 1000);

    /* act */
    int too_soon = ps_gate_allow(&g, 1499);
    int later = ps_gate_allow(&g, 1999);

    /* assert */
    CHECK(!too_soon, "499 ms later is the same press");
    CHECK(later, "a person pressing again half a second later is heard");
}

static void test_ps_gate_failed_holdsPressesBack(void)
{
    /* arrange */
    ps_gate_t g = { 0 };
    ps_gate_allow(&g, 1000);

    /* act */
    ps_gate_failed(&g, 1050);
    int during = ps_gate_allow(&g, 2000);
    int after = ps_gate_allow(&g, 4000);

    /* assert */
    CHECK(!during, "no attach right after one was refused");
    CHECK(after, "it tries again once the wait is over");
}

int main(void)
{
    test_ps_gate_allow_firstPress_goesThrough();
    test_ps_gate_allow_burst_countsAsOnePress();
    test_ps_gate_allow_longBurst_staysBlocked();
    test_ps_gate_allow_afterAPause_goesThrough();
    test_ps_gate_failed_holdsPressesBack();
    printf("test_ps_gate: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
