/* Button remapping (src/remap.c). The first guarantee is the one that keeps working pads working:
 * no rules, no change, and a pad with no rules of its own is left alone. */
#include "remap.h"

#include <stdio.h>
#include <string.h>

static int failures, checks;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

#define RULES_FILE "build/test_remap.txt"
#define DS4_VID 0x054C
#define DS4_PID 0x09CC

static void fresh(void)
{
    remove(RULES_FILE);
    remap_init(RULES_FILE);
}

static pad_state_t ds4_state(uint32_t buttons)
{
    pad_state_t st;
    pad_state_neutral(&st);
    st.vid = DS4_VID;
    st.pid = DS4_PID;
    st.buttons = buttons;
    return st;
}

static uint32_t mapped(uint32_t buttons)
{
    pad_state_t st = ds4_state(buttons);
    remap_apply(&st);
    return st.buttons;
}

static void test_remap_active_noRules_isOff(void)
{
    /* arrange */
    fresh();

    /* act */
    int active = remap_active();

    /* assert */
    CHECK(!active, "with no rules, vpad_update does not even copy the state");
}

static void test_remap_apply_noRulesForThisPad_leavesItUntouched(void)
{
    /* arrange */
    fresh();
    remap_set(DS4_VID, DS4_PID, "cross=circle");
    pad_state_t st, before;
    pad_state_neutral(&st);
    st.vid = 0x045E;
    st.pid = 0x0B13;
    st.buttons = PAD_BTN_CROSS | PAD_BTN_L2;
    st.l2 = 200;
    before = st;

    /* act */
    remap_apply(&st);

    /* assert */
    CHECK(memcmp(&st, &before, sizeof st) == 0, "another model's state stays byte for byte the same");
}

static void test_remap_apply_singleButton_movesIt(void)
{
    /* arrange */
    fresh();
    CHECK(remap_set(DS4_VID, DS4_PID, "create=touchpad"), "the rule is taken");

    /* assert */
    CHECK(mapped(PAD_BTN_CREATE) == PAD_BTN_TOUCHPAD, "Create becomes the touchpad click");
    CHECK(mapped(PAD_BTN_CROSS) == PAD_BTN_CROSS, "other buttons pass");
    CHECK(mapped(PAD_BTN_CREATE | PAD_BTN_CROSS) == (PAD_BTN_TOUCHPAD | PAD_BTN_CROSS), "held with another, both count");
    CHECK(mapped(0) == 0, "nothing pressed gives nothing");
}

static void test_remap_apply_swap_exchangesBoth(void)
{
    /* arrange */
    fresh();
    remap_set(DS4_VID, DS4_PID, "cross=circle,circle=cross");

    /* assert */
    CHECK(mapped(PAD_BTN_CROSS) == PAD_BTN_CIRCLE, "cross reads as circle");
    CHECK(mapped(PAD_BTN_CIRCLE) == PAD_BTN_CROSS, "circle reads as cross");
    CHECK(mapped(PAD_BTN_CROSS | PAD_BTN_CIRCLE) == (PAD_BTN_CROSS | PAD_BTN_CIRCLE), "both give both");
}

static void test_remap_apply_combo_firesOnlyWithAllHeld(void)
{
    /* arrange: the GameSir Nova 2 Lite has no touchpad button (Reddit, 2 Nov) */
    fresh();
    remap_set(DS4_VID, DS4_PID, "l3+r3=touchpad");

    /* assert */
    CHECK(mapped(PAD_BTN_L3 | PAD_BTN_R3) == PAD_BTN_TOUCHPAD, "the combo gives the touchpad and hides its parts");
    CHECK(mapped(PAD_BTN_L3) == PAD_BTN_L3, "L3 alone stays L3");
    CHECK(mapped(PAD_BTN_R3) == PAD_BTN_R3, "R3 alone stays R3");
}

static void test_remap_apply_comboAndSingle_comboWins(void)
{
    /* arrange: the single rule comes first in the text on purpose */
    fresh();
    remap_set(DS4_VID, DS4_PID, "l3=square,l3+r3=touchpad");

    /* assert */
    CHECK(mapped(PAD_BTN_L3 | PAD_BTN_R3) == PAD_BTN_TOUCHPAD, "the longer combo takes L3");
    CHECK(mapped(PAD_BTN_L3) == PAD_BTN_SQUARE, "L3 alone follows its own rule");
}

static void test_remap_apply_comboToPs_detectedAsPress(void)
{
    /* arrange: a pad with no PS button still has to reach the profile screen */
    fresh();
    remap_set(DS4_VID, DS4_PID, "create+options=ps");

    /* assert */
    CHECK(mapped(PAD_BTN_CREATE | PAD_BTN_OPTIONS) == PAD_BTN_PS, "the combo reads as PS");
}

static void test_remap_apply_triggerToButton_leavesTheAxis(void)
{
    /* arrange */
    fresh();
    remap_set(DS4_VID, DS4_PID, "l2=cross");
    pad_state_t pressed = ds4_state(PAD_BTN_L2), light = ds4_state(0);
    pressed.l2 = 200;
    light.l2 = 20;

    /* act */
    remap_apply(&pressed);
    remap_apply(&light);

    /* assert */
    CHECK(pressed.buttons == PAD_BTN_CROSS && pressed.l2 == 0, "pressed: cross, and no L2 travel, got %u", pressed.l2);
    CHECK(light.buttons == 0 && light.l2 == 0, "a light touch reaches nothing, got %u", light.l2);
}

static void test_remap_apply_buttonToTrigger_pressesItFully(void)
{
    /* arrange */
    fresh();
    remap_set(DS4_VID, DS4_PID, "l1=r2");
    pad_state_t st = ds4_state(PAD_BTN_L1), released = ds4_state(0);

    /* act */
    remap_apply(&st);
    remap_apply(&released);

    /* assert */
    CHECK(st.buttons == PAD_BTN_R2 && st.r2 == 255, "the game sees R2 fully in, got %u", st.r2);
    CHECK(released.buttons == 0 && released.r2 == 0, "released, it comes back");
}

static void test_remap_apply_swapTriggers_carriesTravel(void)
{
    /* arrange */
    fresh();
    remap_set(DS4_VID, DS4_PID, "l2=r2,r2=l2");
    pad_state_t half = ds4_state(PAD_BTN_L2), light = ds4_state(0);
    half.l2 = 128;
    light.r2 = 20;

    /* act */
    remap_apply(&half);
    remap_apply(&light);

    /* assert */
    CHECK(half.r2 == 128 && half.l2 == 0 && half.buttons == PAD_BTN_R2, "half of L2 is half of R2, got %u %u", half.r2, half.l2);
    CHECK(light.l2 == 20 && light.r2 == 0, "travel under the digital threshold also moves, got %u %u", light.l2, light.r2);
}

static void test_remap_apply_sticksAndRest_notTouched(void)
{
    /* arrange */
    fresh();
    remap_set(DS4_VID, DS4_PID, "cross=circle");
    pad_state_t st = ds4_state(PAD_BTN_CROSS);
    st.lx = 11; st.ry = 22; st.gyro[1] = 33; st.battery_level = 44; st.touch_x = 55;

    /* act */
    remap_apply(&st);

    /* assert */
    CHECK(st.lx == 11 && st.ry == 22 && st.gyro[1] == 33 && st.battery_level == 44 && st.touch_x == 55,
          "only buttons and triggers change");
}

static void test_remap_set_badText_changesNothing(void)
{
    /* arrange */
    static const char *const bad[] = {
        "ps=cross",             /* PS is how a user reaches the profile screen */
        "ps+cross=circle",
        "cross=cross",
        "cross=",
        "=cross",
        "cross",
        "banana=cross",
        "cross=banana",
        "cross+cross=circle",
        "cross+=circle",
        "+cross=circle",
        "cross=circle,",
        "cross=circle,cross=square",
        "cross+circle+square+triangle=l1",
        "cross=circle,circle=square,square=triangle,triangle=l1,l1=r1,r1=l2,l2=r2,r2=l3,l3=r3",
        "cross=circle=square",
    };
    fresh();
    remap_set(DS4_VID, DS4_PID, "cross=circle");

    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; i++) {
        /* act */
        int ok = remap_set(DS4_VID, DS4_PID, bad[i]);

        /* assert */
        CHECK(!ok, "refused: %s", bad[i]);
        CHECK(mapped(PAD_BTN_CROSS) == PAD_BTN_CIRCLE, "the rules stay after refusing: %s", bad[i]);
    }
}

static void test_remap_set_empty_clearsAndTurnsOff(void)
{
    /* arrange */
    fresh();
    remap_set(DS4_VID, DS4_PID, "cross=circle");

    /* act */
    int ok = remap_set(DS4_VID, DS4_PID, "");

    /* assert */
    CHECK(ok, "clearing is accepted");
    CHECK(!remap_active(), "with the last rule gone, everyone is back on the path without copies");
    CHECK(mapped(PAD_BTN_CROSS) == PAD_BTN_CROSS, "cross is cross again");
}

static void test_remap_init_savedRules_comeBackAfterRestart(void)
{
    /* arrange */
    fresh();
    remap_set(DS4_VID, DS4_PID, "l3+r3=touchpad,cross=circle");
    remap_set(0x045E, 0x0B13, "create=square");

    /* act */
    remap_init(RULES_FILE);

    /* assert */
    char json[512];
    remap_json(json, sizeof json);
    CHECK(remap_active(), "the rules are back");
    CHECK(mapped(PAD_BTN_L3 | PAD_BTN_R3) == PAD_BTN_TOUCHPAD, "the DS4's combo works");
    CHECK(strstr(json, "\"vid\":1356,\"pid\":2508") && strstr(json, "l3+r3=touchpad"), "the page sees them: %s", json);
    CHECK(strstr(json, "\"vid\":1118,\"pid\":2835,\"rules\":\"create=square\""), "for each model: %s", json);
}

static void test_remap_init_damagedFile_keepsTheGoodLines(void)
{
    /* arrange */
    FILE *f = fopen(RULES_FILE, "w");
    fputs("garbage\n054c:09cc ps=cross\n054c:09cc cross=circle\n\n", f);
    fclose(f);

    /* act */
    remap_init(RULES_FILE);

    /* assert */
    CHECK(mapped(PAD_BTN_CROSS) == PAD_BTN_CIRCLE, "a damaged line does not lose the good one");
    CHECK(mapped(PAD_BTN_PS) == PAD_BTN_PS, "PS stays PS");
}

static void test_remap_set_tableFull_refusesNewModelOnly(void)
{
    /* arrange */
    fresh();
    for (int i = 0; i < REMAP_MAX_PADS; i++)
        CHECK(remap_set(0x1000, (uint16_t)i, "cross=circle"), "room for model %d", i);

    /* act */
    int extra = remap_set(0x2000, 1, "cross=circle");
    int same = remap_set(0x1000, 3, "cross=square");

    /* assert */
    CHECK(!extra, "a new model is refused when the table is full");
    CHECK(same, "a known model can still change");
}

int main(void)
{
    test_remap_active_noRules_isOff();
    test_remap_apply_noRulesForThisPad_leavesItUntouched();
    test_remap_apply_singleButton_movesIt();
    test_remap_apply_swap_exchangesBoth();
    test_remap_apply_combo_firesOnlyWithAllHeld();
    test_remap_apply_comboAndSingle_comboWins();
    test_remap_apply_comboToPs_detectedAsPress();
    test_remap_apply_triggerToButton_leavesTheAxis();
    test_remap_apply_buttonToTrigger_pressesItFully();
    test_remap_apply_swapTriggers_carriesTravel();
    test_remap_apply_sticksAndRest_notTouched();
    test_remap_set_badText_changesNothing();
    test_remap_set_empty_clearsAndTurnsOff();
    test_remap_init_savedRules_comeBackAfterRestart();
    test_remap_init_damagedFile_keepsTheGoodLines();
    test_remap_set_tableFull_refusesNewModelOnly();
    remove(RULES_FILE);
    printf("test_remap: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
