/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 RONDO contributors. Built on Felucca (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The screen as a touch surface (the Spelunkr groovebox, Nov 23 2026: Ed, "would it be possible for us to make the
 * display be clickable? I have future dreams of turning this web app into a stand alone groove box with a touch
 * screen"). The web page sends its clicks and fingers here (felucca_web.c web_touch); a touch panel's driver would call
 * ui_touch the same way. What can be touched is what the screen shows now: the zones its drawing left (gfx.c tz_*).
 *   a card (KNOB 1..4)       drag up / right to turn its knob up, down / left down (a step every TOUCH_STEP px)
 *   a keycap / key hint      its button, pressed while the finger is down (OCT+ CLEAR: OCT+); K1..K4, SELECT, ALGO,
 *                            PRESETS: a tap turns that knob a step on
 *   the tracker              a tap on a cell: the cursor to it (its row and column); on the cursor's own drum cell:
 *                            the hit on / off; a drag up / down: the steps (the finger pushes the list), sideways: the
 *                            columns
 *   a layer map's cell        its key, held while the finger is down (FX KEYS: the effect; GLO: SOLO, MUTE ..; SEQ
 *                            TOOLS, REC, EDIT, ARP: what the key does there)
 *   the MENU's rows          a tap picks the row; on the picked row: its next value, or OCT+ (OPEN, CLEAR, ...)
 * ui_touch(phase, x, y): phase TOUCH_DOWN / TOUCH_MOVE / TOUCH_UP, x y in layout pixels (0..239).
 * TOUCH_BTN(b, on): the HAL's: button b (B_*) held by touch or let go (a tap's release at least a frame later). */
#ifndef TOUCH_BTN
#define TOUCH_BTN(b, on) ((void)(b), (void)(on))
#endif
#ifndef TOUCH_KEY                                       /* key k (0..26) held by touch or let go: the HAL's */
#define TOUCH_KEY(k, on) ((void)(k), (void)(on))
#endif
enum { TOUCH_DOWN, TOUCH_MOVE, TOUCH_UP };
#define TOUCH_STEP 8                                    /* px of drag a knob step (a card) */
#define TOUCH_SLOP 5                                    /* px a finger may wander and still tap */

static struct {
    uint8_t on, moved, btn;                             /* btn: B_* + 1 held by this touch, 0 none */
    int8_t key;                                         /* the key held by this touch, -1 none */
    tzone_t z;                                          /* what it came down on (a copy: the zones move under it) */
    int16_t x0, y0, x, y;
    int16_t ax, ay;                                     /* the drag left over (px, under a step) */
} tch;

static const int8_t TOUCH_KC_BTN[KC_COUNT] = {           /* a keycap's button (B_* + 1), 0: none */
    [KC_OCTDN] = B_OCTDN + 1, [KC_OCTUP] = B_OCTUP + 1, [KC_PLAY] = B_PLAY + 1, [KC_REC] = B_REC + 1,
    [KC_SAVE] = B_SAVE + 1, [KC_EDIT] = B_EDIT + 1, [KC_SEQ] = B_SEQ + 1, [KC_GLO] = B_GLO + 1,
    [KC_HOME] = B_HOME + 1, [KC_ARP] = B_ARP + 1, [KC_FX] = B_FX + 1, [KC_SCL] = B_SCL + 1, [KC_LFO] = B_LFO + 1,
    [KC_ENV] = B_ENV + 1, [KC_OCTPM] = B_OCTUP + 1, [KC_ARM] = B_REC + 1,
};
static int32_t touch_kc_enc(uint32_t id)                 /* a keycap that names a knob: its role, -1 none */
{
    return id == KC_SELECT ? EN_SELECT : id == KC_ALGO ? EN_ALGO : id == KC_PRESETS ? EN_PRESET :
           id >= KC_K1 && id <= KC_K4 ? (int32_t)(EN_K1 + id - KC_K1) : -1;
}

/* the tracker: the cursor to step si, column col (255: the column stays); on the cursor's own drum cell, the hit */
static void touch_cell(uint32_t col, int32_t si)
{
    track_t *t = TSEL;
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = tk_ncols(t);
    uint8_t *c = &tk.col[song.sel % NTRK];
    if (si < 0 || (uint32_t)si >= len)
        return;
    if (col != 255u && col < n && (uint32_t)si == ui.cursor && col == *c) {
        uint32_t kind = tk_colkind(t, col);
        if (kind >= TC_BD && kind <= TC_PC) {           /* a drum cell picked already: its hit on / off */
            char b[8];
            tk_turn(0, tk_cell(t, (uint32_t)si, kind, b) == 1 ? -1 : 1);
            ui.force = 1;
        }
        return;
    }
    if (col != 255u && col < n)
        *c = (uint8_t)col;
    cursor_move(si);
    ui.force = 1;
}
static void touch_menu_row(uint32_t k, uint32_t row)
{
    if (ui.menu != 1)
        return;
    if (row != ui.menu_sel) {
        uint32_t t = MI_TAB[row % MI_COUNT];
        ui.menu_sel = (uint8_t)row;
        if (t < sizeof ui.menu_row)
            ui.menu_row[t] = (uint8_t)k;
        ui.force = 1;
    } else if (menu_valued(row)) {
        menu_put(row, menu_step(row, 1));
        ui.force = 1;
    } else {
        TOUCH_BTN(B_OCTUP, 1);                          /* OPEN / CLEAR / ...: as OCT+ */
        TOUCH_BTN(B_OCTUP, 0);
    }
}

static void ui_touch(uint32_t phase, int32_t x, int32_t y)
{
    if (phase == TOUCH_DOWN) {
        const tzone_t *z = tz_at(x, y);
        if (tch.btn)                                    /* (a second finger: the first one's button let go) */
            TOUCH_BTN(tch.btn - 1u, 0);
        if (tch.on && tch.key >= 0)
            TOUCH_KEY((uint32_t)tch.key, 0);
        memset(&tch, 0, sizeof tch);
        tch.key = -1;
        tch.on = 1;
        tch.x0 = tch.x = (int16_t)x;
        tch.y0 = tch.y = (int16_t)y;
        if (z)
            tch.z = *z;
        if (tch.z.kind == TZ_KEY && tch.z.a < KC_COUNT && TOUCH_KC_BTN[tch.z.a]) {
            tch.btn = (uint8_t)TOUCH_KC_BTN[tch.z.a];
            TOUCH_BTN(tch.btn - 1u, 1);
        } else if (tch.z.kind == TZ_NOTE) {
            tch.key = (int8_t)tch.z.a;
            TOUCH_KEY(tch.z.a, 1);
        }
        scrn.idle = fm1_ms;                             /* (a touch wakes the screen as a knob would) */
        return;
    }
    if (!tch.on)
        return;
    if (phase == TOUCH_MOVE) {
        int32_t dx = x - tch.x, dy = y - tch.y, s;
        if (!tch.moved && (x - tch.x0) * (x - tch.x0) + (y - tch.y0) * (y - tch.y0) < TOUCH_SLOP * TOUCH_SLOP)
            return;
        tch.moved = 1;
        tch.x = (int16_t)x;
        tch.y = (int16_t)y;
        if (tch.z.kind == TZ_CARD) {                    /* up and right turn it up */
            tch.ay = (int16_t)(tch.ay + dx - dy);
            if ((s = tch.ay / TOUCH_STEP) != 0) {
                tch.ay = (int16_t)(tch.ay - s * TOUCH_STEP);
                touch_steps[EN_K1 + (tch.z.a & 3u)] += s;
            }
        } else if (tch.z.kind == TZ_CELL) {             /* the finger pushes the list: up = later steps */
            tch.ay = (int16_t)(tch.ay - dy);
            tch.ax = (int16_t)(tch.ax - dx);
            if ((s = tch.ay / TK_ROW) != 0) {
                tch.ay = (int16_t)(tch.ay - s * TK_ROW);
                touch_steps[EN_K1] += s;
            }
            if ((s = tch.ax / 22) != 0) {
                tch.ax = (int16_t)(tch.ax - s * 22);
                touch_steps[EN_K2] += s;
            }
        }
        return;
    }
    /* TOUCH_UP */
    tch.on = 0;
    if (tch.key >= 0) {                                 /* (a key: let go wherever the finger went) */
        TOUCH_KEY((uint32_t)tch.key, 0);
        tch.key = -1;
        return;
    }
    if (tch.btn) {
        TOUCH_BTN(tch.btn - 1u, 0);
        tch.btn = 0;
        return;
    }
    if (tch.moved)
        return;
    switch (tch.z.kind) {                               /* a tap */
    case TZ_KEY: {
        int32_t r = touch_kc_enc(tch.z.a);
        if (r >= 0)
            touch_steps[r] += 1;
        break;
    }
    case TZ_CELL:
        touch_cell(tch.z.a, tch.z.b);
        break;
    case TZ_ROW:
        touch_menu_row(tch.z.a, (uint32_t)tch.z.b);
        break;
    default:
        break;
    }
}
