#include "joycon_join.h"

#include <string.h>

#define JOYCON_VID 0x057E
#define JOYCON_L_PID 0x2006
#define JOYCON_R_PID 0x2007

void joycon_start(joycon_t *j, uint16_t vid, uint16_t pid)
{
    memset(j, 0, sizeof(*j));
    j->mate = -1;
    pad_state_reset(&j->last);
    if (vid != JOYCON_VID) j->side = JOYCON_NONE;
    else if (pid == JOYCON_L_PID) j->side = JOYCON_LEFT;
    else if (pid == JOYCON_R_PID) j->side = JOYCON_RIGHT;
    else j->side = JOYCON_NONE;
}

int joycon_can_pair(const joycon_t *a, const joycon_t *b)
{
    return a->side != JOYCON_NONE && b->side != JOYCON_NONE && a->side != b->side && a->mate < 0 && b->mate < 0;
}

static uint32_t map_buttons(uint32_t in, const uint32_t table[][2], int n)
{
    uint32_t out = 0;
    for (int i = 0; i < n; i++) {
        if (in & table[i][0]) out |= table[i][1];
    }
    return out;
}

/* Sideways, the profile names each button by where it sits: this undoes it. SL and SR have no place
 * on one controller held upright. Capture is the touchpad, as on the Switch Pro profile. */
static const uint32_t k_left_buttons[][2] = {
    { PAD_CROSS, PAD_LEFT }, { PAD_CIRCLE, PAD_DOWN }, { PAD_SQUARE, PAD_UP }, { PAD_TRIANGLE, PAD_RIGHT },
    { PAD_L2, PAD_L1 }, { PAD_R2, PAD_L2 }, { PAD_CREATE, PAD_CREATE }, { PAD_OPTIONS, PAD_TOUCHPAD },
    { PAD_L3, PAD_L3 },
};

static const uint32_t k_right_buttons[][2] = {
    { PAD_CROSS, PAD_CIRCLE }, { PAD_CIRCLE, PAD_TRIANGLE }, { PAD_SQUARE, PAD_CROSS }, { PAD_TRIANGLE, PAD_SQUARE },
    { PAD_L2, PAD_R1 }, { PAD_R2, PAD_R2 }, { PAD_OPTIONS, PAD_OPTIONS }, { PAD_PS, PAD_PS },
    { PAD_L3, PAD_R3 },
};

void joycon_merge(pad_state *out, const pad_state *left, const pad_state *right)
{
    pad_state_reset(out);
    out->buttons = map_buttons(left->buttons, k_left_buttons, (int)(sizeof k_left_buttons / sizeof k_left_buttons[0])) |
                   map_buttons(right->buttons, k_right_buttons, (int)(sizeof k_right_buttons / sizeof k_right_buttons[0]));

    /* The opposite of the quarter turn in joycon_parse */
    out->lx = (uint8_t)(255 - left->ly);
    out->ly = left->lx;
    out->rx = right->ly;
    out->ry = (uint8_t)(255 - right->lx);

    out->l2 = out->buttons & PAD_L2 ? 255 : 0;
    out->r2 = out->buttons & PAD_R2 ? 255 : 0;

    /* The one that runs out first decides */
    const pad_state *low = left;
    if (!left->has_battery || (right->has_battery && right->battery_pct < left->battery_pct)) low = right;
    out->has_battery = low->has_battery;
    out->battery_pct = low->battery_pct;
    out->charging = low->charging;
}
