/* The recorder behind /api/capture_* (src/capture.c). */
#include "capture.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static char g_out[65536];

static int read_all(int *total, int *state)
{
    int next = 0;
    return capture_read(0, g_out, (int)sizeof g_out, &next, total, state);
}

static void test_capture_wants_off_recordsNothing(void)
{
    /* arrange */
    static const uint8_t rep[] = { 0x01, 0x80 };
    int total = -1, state = -1;

    /* act */
    int wants = capture_wants(0);
    capture_add(CAPTURE_KIND_USB, 0, 0x054c, 0x05c4, rep, sizeof rep, 1, 0, 128, 128, 128, 128, 0, 0);
    read_all(&total, &state);

    /* assert */
    CHECK(!wants, "nothing is recorded until the API asks");
    CHECK(total == 0 && state == 0, "no entries and state off, got %d entries, state %d", total, state);
}

static void test_capture_add_report_isReadAsTheLineTheReplayExpects(void)
{
    /* arrange: a DS4 report on USB, from tests/test_drivers.c */
    static const uint8_t rep[] = { 0x01, 0x7e, 0x80, 0x82, 0x82, 0x08, 0x00, 0x54 };
    capture_start(1, 60);

    /* act */
    capture_mark("press cross");
    capture_add(CAPTURE_KIND_USB, 1, 0x054c, 0x05c4, rep, sizeof rep, 1, 0x4000, 126, 128, 130, 130, 0, 0);
    int total = 0, state = 0;
    read_all(&total, &state);

    /* assert */
    char kind, hex[64];
    int t, slot, len, result, a[6];
    unsigned vid, pid, buttons;
    const char *line = strchr(g_out, '\n');
    int n = line ? sscanf(line + 1, "%c %d %d %x %x %d %63s %d %u %d %d %d %d %d %d", &kind, &t, &slot, &vid, &pid,
                          &len, hex, &result, &buttons, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) : 0;
    CHECK(total == 2 && state == 1, "a mark and a report while recording, got %d, state %d", total, state);
    CHECK(strncmp(g_out, "M ", 2) == 0 && strstr(g_out, "press_cross\n"), "the mark keeps its words, got %s", g_out);
    CHECK(n == 15, "the report line has the 15 fields tests/test_captures.c reads, got %d", n);
    CHECK(kind == 'U' && slot == 1 && vid == 0x054c && pid == 0x05c4 && len == 8, "who sent it");
    CHECK(strcmp(hex, "017e808282080054") == 0, "the bytes as sent, got %s", hex);
    CHECK(result == 1 && buttons == 0x4000 && a[0] == 126 && a[2] == 130, "what the parser made of it");
}

static void test_capture_add_otherSlot_isLeftOut(void)
{
    /* arrange */
    static const uint8_t rep[] = { 0x01 };
    capture_start(2, 60);

    /* act */
    capture_add(CAPTURE_KIND_BT, 3, 0x054c, 0x05c4, rep, sizeof rep, 1, 0, 0, 0, 0, 0, 0, 0);
    capture_add(CAPTURE_KIND_BT, 2, 0x054c, 0x05c4, rep, sizeof rep, 1, 0, 0, 0, 0, 0, 0, 0);
    int total = 0, state = 0;
    read_all(&total, &state);

    /* assert */
    CHECK(total == 1 && g_out[0] == 'B', "only the slot asked for, got %d entries", total);
}

static void test_capture_add_longReport_isMarkedCut(void)
{
    /* arrange */
    uint8_t rep[200];
    memset(rep, 0xab, sizeof rep);
    capture_start(0, 60);

    /* act */
    capture_add(CAPTURE_KIND_USB, 0, 0x2dc8, 0x3106, rep, sizeof rep, 1, 0, 0, 0, 0, 0, 0, 0);
    int total = 0, state = 0;
    read_all(&total, &state);

    /* assert */
    CHECK(strstr(g_out, " 200 - ") != NULL, "a report too long to keep is marked, so replay skips it: %s", g_out);
}

static void test_capture_add_bufferFull_stopsTheRecording(void)
{
    /* arrange */
    static const uint8_t rep[] = { 0x01, 0x02 };
    capture_start(0, 300);

    /* act */
    for (int i = 0; i < 50000; i++)
        capture_add(CAPTURE_KIND_USB, 0, 0x2dc8, 0x3106, rep, sizeof rep, 1, 0, 0, 0, 0, 0, 0, 0);
    int total = 0, state = 0, next = 0;
    capture_read(0, g_out, 4096, &next, &total, &state);

    /* assert */
    CHECK(state == 3 && total < 50000, "a full buffer ends the recording and says so, state %d, %d entries", state, total);
    CHECK(next > 0 && next < total, "a page holds what fits, and says where the next one starts (%d)", next);
    CHECK(!capture_wants(0), "nothing more is taken");
}

static void test_capture_stop_keepsWhatWasRecorded(void)
{
    /* arrange */
    static const uint8_t rep[] = { 0x01 };
    capture_start(0, 60);
    capture_add(CAPTURE_KIND_USB, 0, 0x054c, 0x05c4, rep, sizeof rep, 1, 0, 0, 0, 0, 0, 0, 0);

    /* act */
    capture_stop();
    capture_add(CAPTURE_KIND_USB, 0, 0x054c, 0x05c4, rep, sizeof rep, 1, 0, 0, 0, 0, 0, 0, 0);
    int total = 0, state = 0;
    read_all(&total, &state);

    /* assert */
    CHECK(total == 1 && state == 2, "stopped: one entry kept, state finished, got %d, state %d", total, state);
}

int main(void)
{
    test_capture_wants_off_recordsNothing();
    test_capture_add_report_isReadAsTheLineTheReplayExpects();
    test_capture_add_otherSlot_isLeftOut();
    test_capture_add_longReport_isMarkedCut();
    test_capture_add_bufferFull_stopsTheRecording();
    test_capture_stop_keepsWhatWasRecorded();
    printf("test_capture: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
