/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * The design (the device clock driven by the audio the worklet asks for, the exports below, the saved
 * flash handed back before boot) follows X0X's browser build, web/emu/x0x_web.c of charlesvestal/fm1-x0x
 * (GPL-3.0), a Felucca fork. */
/* Felucca in the browser: the firmware's sources, as the host tests build them (tests/hostsim.c: libc,
 * engines, voices, FX, sequencer; then the UI, main.c, storage and projects, in src/felucca.c's order),
 * compiled to WebAssembly and run in an AudioWorklet against a simulated FM-1:
 *   - the device clock: TIMER4 ticks (24 MHz) and fm1_ms advance with the audio the worklet asks for; the
 *     audio ISR (audio.c fm1_alnk0_irq, the real one) renders each I2S half (HALF_FRAMES) as the DMA would
 *     ask for it, and the main loop (main.c's, below in web_frame) runs every 15 ms, with ui_input on every
 *     millisecond in between as main.c's wait loop calls it;
 *   - the panel: buttons and encoders by their printed labels (panel.c's table maps them to the matrix),
 *     the keys as fm1_in.notes, the LEDs read back from fm1_led / fm1_led_dim through the key matrix;
 *   - the screen: lcd_fill / lcd_blit into a 240 x 240 RGB565 frame (the panel's byte order);
 *   - the flash: a 1 MiB NOR image behind storage.c's hooks (settings, projects, user presets, the user
 *     sample slots read through it as XIP); the page keeps its storage sectors and gives them back at boot.
 * Not here: USB (MIDI, audio, the editor, updates), TRS MIDI, the serial console, the battery (full), the
 * crash / UBOOT paths. Nothing of the firmware changes for this file.
 *   web/emu/build.sh  ->  build/emu/{felucca.wasm, worklet.js, index.html} */
#include <stdint.h>

#define FELUCCA_WEB 1
#define SPK_USER 8                       /* the user samples (core.h): the page's SAMPLES panel */
#include <stdarg.h>
#include <stdio.h>
static unsigned char web_solo;           /* the page's SOLO buttons (ui_layer.c WEB_SOLO): FEEDs by index */
static unsigned char web_pad;            /* the page's FX PAD held (ui_input.c WEB_PAD) */
#define WEB_SOLO web_solo
#define WEB_PAD web_pad
/* the knobs' cards as the screen drew them this frame (ui_draw.c draw_column: WEB_CARD), kept per frame */
static struct { char label[16], val[16], unit[8]; int ratio; } web_card[4], web_card_now[4];
static unsigned web_cards_seen, web_cards_shown;
static void web_card_put(unsigned c, const char *l, const char *v, const char *u, int r)
{
    if (c >= 4u)
        return;
    snprintf(web_card[c].label, sizeof web_card[c].label, "%s", l);
    snprintf(web_card[c].val, sizeof web_card[c].val, "%s", v);
    snprintf(web_card[c].unit, sizeof web_card[c].unit, "%s", u);
    web_card[c].ratio = r;
    web_cards_seen |= 1u << c;
}
#define WEB_CARD(c, label, val, unit, ratio) web_card_put((c), (label), (val), (unit), (ratio))
#ifndef FELUCCA_CDC
#define FELUCCA_CDC 1                    /* as src/felucca.c: MENU > USB SERIAL is there (nothing behind it) */
#endif
#define FELUCCA_FLASH 1
#define FELUCCA_OTA 0
#define FELUCCA_UAC 0
#define FELUCCA_UART 0
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "WEB"
#endif

static uint8_t nor[0x100000];            /* the SPI NOR (erased: 0xFF) */
#define SMP_USER_XIP(k) ((const uint8_t *)nor + SMP_USER_BASE + (k) * SMP_USER_SIZE)   /* eng_sample.c */
#define main hostsim_main
#include "../../tests/hostsim.c"
#undef main

/* ------------------------------------------------------------ the clock --- */
#define FM1_TICKS_PER_US 24u             /* TIMER4: 24 MHz */
static uint32_t web_ticks;
static uint32_t fm1_ticks(void) { return web_ticks; }

/* ------------------------------------------------------ audio (ALNK0) --- */
static uint32_t web_half;                /* the half the next render fills */
#define FM1_AUDIO_HALF 1u
static uint8_t fm1_audio_pending(void) { return FM1_AUDIO_HALF; }
static void fm1_audio_ack_aux(uint8_t p) { (void)p; }
static uint32_t fm1_audio_free_half(void) { return web_half; }
static void fm1_audio_ack_half(void) {}
static void fm1_audio_init(int32_t *buf, uint32_t words, void (*isr)(void), uint32_t prio)
{ (void)buf; (void)words; (void)isr; (void)prio; }
static void fm1_audio_stop(void) {}
void isr_alnk0(void) {}
#include "../../firmware/src/audio.c"

/* ------------------------------------------------------------ the panel --- */
#define FM1_NCOL 11u
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* as hal/fm1_input.h: key id at (column, row bit) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_breath[FM1_NCOL], fm1_led_mid[FM1_NCOL];
static uint32_t web_dim_lo;
static void fm1_led_dim_level(uint32_t lo) { web_dim_lo = lo; }
/* the power-on LED sweep (hal/fm1_input.h fm1_led_anim_start, the picture hal/fm1_led_anim.h): its frame from the
 * clock (a scan frame 1.1 ms), the page draws each LED's level (web_anim_levels); at its end, or on a key or a button
 * down, the last picture (the glow everywhere, or dark) and the UI's LEDs from the next frame, as the scan does */
#include "../../firmware/hal/fm1_led_anim.h"
static uint32_t web_anim_t0, web_anim_glow, web_anim_run, web_anim_idle;   /* (idle: 1.2's idle animation, MENU >
                                                                              * ANIM IDLE: fm1_idle_level, endless) */
static uint32_t web_anim_frame(void) { return (web_ticks - web_anim_t0) / (1100u * FM1_TICKS_PER_US); }
static void fm1_led_anim_start(uint32_t glow)
{
    web_anim_idle = 0;
    memset(fm1_led, 0, sizeof fm1_led);
    memset(fm1_led_dim, 0, sizeof fm1_led_dim);
    memset(fm1_led_breath, 0, sizeof fm1_led_breath);
    memset(fm1_led_mid, 0, sizeof fm1_led_mid);
    web_anim_t0 = web_ticks;
    web_anim_glow = glow != 0u;
    web_anim_run = 1;
}
static int fm1_led_anim_on(void)
{
    if (web_anim_run && web_anim_idle && (fm1_in.notes || fm1_in.buttons))
        web_anim_run = 0;                       /* (the idle animation: ended, the UI's LEDs from now) */
    if (web_anim_run && !web_anim_idle && (web_anim_frame() >= FM1_ANIM_FRAMES || fm1_in.notes || fm1_in.buttons)) {
        uint32_t c, r;
        web_anim_run = 0;
        for (c = 0; c < FM1_NCOL; c++)
            for (r = 1; r < 5u; r++)
                if (web_anim_glow && FM1_KEYMAP[r][c] >= 0)
                    fm1_led_dim[c] |= (uint8_t)(1u << r);
    }
    return (int)web_anim_run;
}
static void fm1_led_idle_start(uint32_t glow)
{
    fm1_led_anim_start(glow);
    web_anim_idle = 1;
}
static int fm1_led_idle_on(void) { return fm1_led_anim_on() && web_anim_idle; }
static void fm1_led_anim_stop(void) { web_anim_run = 0; }
static uint32_t web_pressed, web_released, web_notes_pressed;
static int32_t web_enc_steps[7];
static uint32_t fm1_input_edges(uint32_t *released)
{
    uint32_t p = web_pressed;
    if (released)
        *released = web_released;
    web_pressed = web_released = 0;
    return p;
}
static uint32_t fm1_input_note_edges(void) { uint32_t n = web_notes_pressed; web_notes_pressed = 0; return n; }
static int32_t fm1_enc_take(uint32_t e) { int32_t s = web_enc_steps[e % 7u]; web_enc_steps[e % 7u] = 0; return s; }
static void fm1_input_init(void) {}
static void fm1_input_tick(void) {}
static void fm1_wdt_feed(void) {}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}

/* ----------------------------------------------------------- the screen --- */
#define LCD_SCALE 2                      /* the Spelunkr groovebox: 480 x 480 drawn for the layout's 240 x 240 (gfx.c GFX_SCALE) */
#define LCD_PX (240u * LCD_SCALE)
static uint16_t web_fb[LCD_PX * LCD_PX];   /* RGB565 as the panel gets it (high byte first) */
static uint32_t web_draws;
/* the rows that changed since the worklet last took the frame (web_screen_rows): a write that leaves a pixel as it was
 * changes nothing, so the page copies and converts only what moved (the screen redraws whole regions every frame) */
static uint32_t web_dy0 = LCD_PX, web_dy1;
static void lcd_rows(uint32_t y0, uint32_t y1)
{
    if (y0 < web_dy0) web_dy0 = y0;
    if (y1 > web_dy1) web_dy1 = y1;
    web_draws++;
}
/* x y w h in layout pixels; lcd_blit's p holds the drawn pixels: (w * LCD_SCALE) x (h * LCD_SCALE) */
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j, lo = LCD_PX, hi = 0;
    uint16_t sw = (uint16_t)((c >> 8) | (c << 8));
    x *= LCD_SCALE; y *= LCD_SCALE; w *= LCD_SCALE; h *= LCD_SCALE;
    if (x >= LCD_PX) return;
    if (x + w > LCD_PX) w = LCD_PX - x;
    for (j = 0; j < h && y + j < LCD_PX; j++) {
        uint16_t *row = web_fb + (y + j) * LCD_PX + x;
        uint32_t diff = 0;
        for (i = 0; i < w; i++) {
            diff |= row[i] ^ sw;
            row[i] = sw;
        }
        if (diff) { if (lo == LCD_PX) lo = y + j; hi = y + j + 1; }
    }
    if (hi) lcd_rows(lo, hi);
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j, lo = LCD_PX, hi = 0, cw;
    x *= LCD_SCALE; y *= LCD_SCALE; w *= LCD_SCALE; h *= LCD_SCALE;
    if (x >= LCD_PX) return;
    cw = x + w > LCD_PX ? LCD_PX - x : w;
    for (j = 0; j < h && y + j < LCD_PX; j++) {
        uint16_t *row = web_fb + (y + j) * LCD_PX + x;
        const uint16_t *src = p + j * w;
        uint32_t diff = 0;
        for (i = 0; i < cw; i++) {
            diff |= row[i] ^ src[i];
            row[i] = src[i];
        }
        if (diff) { if (lo == LCD_PX) lo = y + j; hi = y + j + 1; }
    }
    if (hi) lcd_rows(lo, hi);
}
static void lcd_sync(void) {}
static void lcd_power(uint32_t s)              /* MENU > SCREEN OFF (lcd.c): off, the page dark; on: redrawn before */
{
    if (!s) {
        memset(web_fb, 0, sizeof web_fb);
        lcd_rows(0, LCD_PX);
    }
}
static void lcd_wake_now(void) {}
static void lcd_init(void) {}

#include "../../firmware/src/gfx.c"
#include "../../firmware/src/panel.c"
#include "../../firmware/src/ui.c"
#include "../../firmware/src/menu_items.c"
#include "../../firmware/src/icons.c"
#include "../../firmware/src/ui_graph.c"
#include "../../firmware/src/ui_draw.c"
#include "../../firmware/src/ui_menu.c"
#include "../../firmware/src/ui_input.c"
#include "../../firmware/src/ui_layer.c"

/* ------------------------------------------------------------ the flash --- */
static uint32_t web_flash_writes;
static uint8_t flash_ok;
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    if (off >= sizeof nor || n > sizeof nor - off)
        return -1;
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (off >= sizeof nor || 4096u > sizeof nor - off)
        return -8;
    memset(nor + (off & ~4095u), 0xFF, 4096);
    web_flash_writes++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)       /* NOR: a write only clears bits */
{
    const uint8_t *s = src;
    uint32_t i;
    if (off >= sizeof nor || n > sizeof nor - off)
        return -8;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    web_flash_writes++;
    return 0;
}
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }             /* the FM-1's part (project.c) */
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "../../firmware/src/storage.c"
#include "../../firmware/src/upreset.c"
#include "../../firmware/src/project.c"
#include "../../firmware/src/boot_song.c"      /* Dungeon Tracker: the power-on song (main.c) */

/* ------------------------------------------------- main.c and its HAL --- */
#if FELUCCA_CPU2
typedef struct { uint32_t magic, count, vec, pc, emu, dbg, rets, core; } fm1_crash_t;
#else
typedef struct { uint32_t magic, count, vec, pc, emu, dbg, rets; } fm1_crash_t;
#endif
#define FM1_CRASH_MAGIC 0x43525348u
static fm1_crash_t fm1_crash;                     /* (none: main.c boot_leds runs the sweep) */
static struct { uint8_t p3_rst, wdt_con; uint32_t rst_src; } fm1_boot;
uint32_t _data_start[1], _data_end[1], _data_load[1], _bss_start[1], _bss_end[1];
uint32_t _pool_start[1], _pool_end[1], _rt_start[1], _rt_end[1], _rt_load[1];
static uint32_t web_master_adc = 700;   /* MASTER, 0..1023 as the ADC reads it */
enum { FM1_ADC_BATT = 3, FM1_ADC_MASTER = 4 };
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? (int32_t)web_master_adc : 620; }
static void fm1_adc_init(void) {}
static void fm1_timer5_ack(void) {}
static void fm1_timer5_start(void (*isr)(void), uint32_t prio) { (void)isr; (void)prio; }
void isr_timer5(void) {}
static void fm1_reboot(void) {}
static void fm1_enter_uboot(void) {}
static void fm1_guard_lock_top(void) {}
static void fm1_irq_enable_all(void) {}
static void fm1_irq_init(void) {}
static void fm1_time_init(void) {}
static void fm1_reset_reason(void) {}
static void fm1_wdt_arm(uint32_t v) { (void)v; }
static void fm1_mailbox_clear(void) {}
static void fm1_guard_enable(uint32_t m) { (void)m; }
enum { FM1_GUARD_STACK = 1, FM1_GUARD_WRITE = 2, FM1_GUARD_BUS = 4, FM1_GUARD_PC = 8 };
#if FELUCCA_CPU2
/* the second core (hal/fm1_cpu1.h): none here, one core whatever MENU > 2ND CORE says (fx.c mix_block's C1_ON() is 0
 * without felucca.c's macros) */
uint32_t _c1_start[1], _c1_end[1], _c1_load[1];
static uint8_t fm1_c1_on, fm1_c1_gave_up;
static void fm1_cpu1_halt(void) {}
static int fm1_cpu1_start(void) { return -1; }
static void fm1_guard_pc_open(void) {}
#endif
#if FELUCCA_CDC
static void cdc_task(void) {}
#endif
/* main.c's calls into the USB driver (registers): nothing here (usb.c's own functions stay as they are) */
#define usb_poll() ((void)0)
#define usb_start() ((void)0)
#define usb_retry(ms) ((void)(ms))
#define usb_detach() ((void)0)
#include "../../firmware/src/master.c"
#include "../../firmware/src/main.c"
#undef usb_poll
#undef usb_start
#undef usb_retry
#undef usb_detach

/* --------------------------------------------------------- the device --- */
static uint32_t web_booted, web_boot_ms, web_last_frame;
static uint32_t web_shared;                        /* the studio's shared link: the music put in before boot */
static uint32_t web_seed_v;                               /* Dungeon Tracker: the power-on song's seed (the page's) */

/* main.c fm1_main's boot, up to the main loop (no USB, no UART, no panel setup) */
static void spk_boot(void);
static void web_power_on(void)
{
    spk_boot();                                   /* (the user samples' names, before anything shows them) */
    persist_boot();
    settings_init();
    usb_serial_apply();
    lcd_init();
    draw_splash();                        /* main.c: the splash (ui_draw.c) */
    memset(&felucca_dbg, 0, sizeof felucca_dbg);
    felucca_dbg.magic = DBG_MAGIC;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();
    master_boot();                        /* main.c (#137): the pot's level from the first block */
    kb_boot_hold = 1;                     /* main.c (#137): the keys silent under the splash */
    audio_init();
    boot_leds();                          /* main.c: the power-on LED sweep, from the scan's start */
    {
        __typeof__(ui_prefs) prefs = ui_prefs;
        int restored;
        if (web_shared)                   /* a shared song (web_obj_put before boot): restored whatever RESTORE LAST says */
            ui_prefs &= (__typeof__(ui_prefs))~PREF_RESTORE_OFF;
        restored = autosave_boot(boot_clean || web_shared);   /* main.c: the last session's music (1.2, project.c) */
        ui_prefs = prefs;
#if RONDO_LAYOUT
        bank_boot();                      /* main.c: RONDO's PATTERN BANK */
#endif
        (void)restored;                   /* main.c: none of the user's: the default song (1.0: no roll) */
    }
    web_boot_ms = fm1_ms + 430u;         /* main.c: 30 + 400 ms before the first frame */
    web_booted = 1;
}

static uint32_t web_headless;                      /* the studio's bounce (web_bounce_start): a clone that only plays */
/* one pass of main.c fm1_main's loop (what the browser has: no USB, editor, UBOOT, console) */
static void web_frame(void)
{
    int32_t b = fm1_adc_read(FM1_ADC_BATT);
    if (b > 0)
        song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
    master_poll();
    felucca_dbg.ui_frames++;
    ui_input();
    settings_poll();
    if (!web_headless)
        autosave_poll();
#if RONDO_LAYOUT
    bank_poll();
    morph_poll();
#endif
    if (web_headless)                                     /* (a bounce: no screen, no lights) */
        return;
    ui_leds();
    web_cards_seen = 0;
    ui_draw();
    if (web_cards_seen) {                                 /* the cards drawn this frame */
        memcpy(web_card_now, web_card, sizeof web_card);
        web_cards_shown = web_cards_seen;
    } else if (ui.menu || name_on() || ui.layer) {        /* a menu, a name, a layer's map: no cards */
        web_cards_shown = 0;
    }
}

/* output ring, stereo float, filled by web_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r, web_samples_due;

/* one millisecond of the device: the clock, the audio halves due by now, the main loop */
static void web_ms(void)
{
    fm1_ms++;
    web_ticks += 1000u * FM1_TICKS_PER_US;
    web_samples_due += FS;
    while (web_samples_due >= 1000u * HALF_FRAMES) {      /* an I2S half: the audio ISR renders it */
        uint32_t k;
        const int32_t *o = &abuf[web_half * HALF_WORDS];
        web_samples_due -= 1000u * HALF_FRAMES;
        fm1_alnk0_irq();
        for (k = 0; k < HALF_FRAMES; k++) {
            uint32_t i = (ring_w++ % RING) * 2u;
            ring[i] = (float)o[2u * k] / 8388608.0f;
            ring[i + 1] = (float)o[2u * k + 1u] / 8388608.0f;
        }
        web_half ^= 1u;
    }
    if (!web_booted || (int32_t)(fm1_ms - web_boot_ms) < 0)
        return;
    if (web_boot_ms) {                                    /* main.c: the splash goes, the loop starts */
        web_boot_ms = 0;
        kb_boot_hold = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        web_last_frame = fm1_ms - 15u;
    }
    if (fm1_ms - web_last_frame >= 15u) {                /* main.c: a frame, then ui_input until 15 ms are over */
        web_last_frame = fm1_ms;
        web_frame();
    } else {
        ui_input();                                       /* main.c's wait loop */
    }
}

#define EXPORT __attribute__((used, visibility("default")))
#undef __attribute__

static float out_l[1024], out_r[1024];
EXPORT float *web_out_l(void) { return out_l; }
EXPORT float *web_out_r(void) { return out_r; }
/* ---- Spelunkr: VAPOR, on the master (Ed, Nov 18 2026), after the device's own output, so the page's recorder, the
 * bounce and the stems all have it. Four amounts 0..127, kept in the song (song.g's two inert globals, two a slot:
 * G_DRLVL = SLOWED << 8 | WOW, G_DRREV = VINYL << 8 | CRUSH; params.c lets them hold that), so a project file and a
 * share link bring them:
 *   SLOWED  the record played slower, the pitch and the tempo down together (to 0.7: about -6 semitones)
 *   WOW     the tape's warble: a slow wow and a faster flutter on the speed
 *   VINYL   the crackle and the hiss of an old record
 *   CRUSH   the bits (16 down to 6) and the rate (held down to a sixth)
 * All four 0: the device's samples as they are (the path below untouched) */
static uint32_t vap_get(uint32_t k) { return ((uint32_t)(uint16_t)song.g[k < 2u ? G_DRLVL : G_DRREV] >> (k & 1u ? 0 : 8)) & 127u; }
static void vap_set(uint32_t k, uint32_t v)
{
    uint32_t g = k < 2u ? G_DRLVL : G_DRREV, w = (uint16_t)song.g[g], sh = k & 1u ? 0u : 8u;
    w = (w & ~(127u << sh)) | ((v > 127u ? 127u : v) << sh);
    song.g[g] = (int16_t)w;
}
static double sin_d(double cycles) { return sin(6.283185307179586 * cycles); }
static double vap_pos, vap_ph1, vap_ph2;               /* the read position past ring_r (0..1); WOW's phases */
static float vap_hold_l, vap_hold_r, vap_crk, vap_crk_y, vap_hiss_y;
static uint32_t vap_hc, vap_rng = 0x5EED1u;
static float vap_rand(void) { vap_rng = vap_rng * 1664525u + 1013904223u; return (float)(int32_t)vap_rng / 2147483648.0f; }
static double vap_speed(void) { return 1.0 - vap_get(0) * 0.3 / 127.0; }
EXPORT uint32_t web_vapor_get(uint32_t k) { return vap_get(k & 3u); }
/* ---- the user samples (Ed, Nov 22 2026): the page's SAMPLES panel loads them (IMA ADPCM at 22.05 kHz, made by the
 * page), here they play (core.h SPK_NU). Slot k: web_spk_clear(k) (silent at once: no voice reads a half-written
 * slot), the page writes the data at web_spk_data(k) (the whole sample, then each slice, each from a fresh ADPCM
 * state, at even byte offsets), the meta at web_spk_meta() (the whole's samples, its root in 1/16 semitones, the
 * slices' count, then each slice's byte offset and samples) and the name at web_spk_name(), then web_spk_commit(k).
 * They live in the page (its browser storage, the project file), not in the device's flash: a share link has none */
#define SPK_CAP (704u * 1024u)                       /* bytes a slot: ~32 s whole + its slices */
static uint8_t spk_pool[SPK_NU][SPK_CAP];
static uint32_t spk_meta[4 + 2 * SPK_NSLICE];
static char spk_nm[16];
static void spk_names(uint32_t k, const char *nm)
{
    uint32_t i, n = 0;
    for (i = 0; nm[i] && n < 8u; i++) {
        char c = nm[i] >= 'a' && nm[i] <= 'z' ? (char)(nm[i] - 32) : nm[i];
        if (c >= ' ' && c <= '~')
            spk_name[k][n++] = c;
    }
    if (!n) {
        static const char U[] = "USER ";
        for (n = 0; U[n]; n++)
            spk_name[k][n] = U[n];
        spk_name[k][n++] = (char)('1' + k);
    }
    spk_name[k][n] = 0;
    for (i = 0; i < n; i++)
        spk_chop[k][i] = spk_name[k][i];
    spk_chop[k][n] = ' ';
    spk_chop[k][n + 1] = 'C'; spk_chop[k][n + 2] = 'H'; spk_chop[k][n + 3] = 'O'; spk_chop[k][n + 4] = 'P';
    spk_chop[k][n + 5] = 0;
}
EXPORT uint8_t *web_spk_data(uint32_t k) { return spk_pool[k % SPK_NU]; }
EXPORT uint32_t web_spk_cap(void) { return SPK_CAP; }
EXPORT uint32_t *web_spk_meta(void) { return spk_meta; }
EXPORT char *web_spk_name(void) { return spk_nm; }
EXPORT void web_spk_clear(uint32_t k)
{
    uint32_t i;
    k %= SPK_NU;
    usr_nz[k] = usr_nz[SPK_NU + k] = 0;
    for (i = 0; i < SMP_UZ; i++)
        usr_zone[k][i].n = usr_zone[SPK_NU + k][i].n = 0;   /* (a voice on it ends: smp render's n) */
    for (i = 0; i < 8u; i++)
        spk_hit_n[k * 8u + i] = 0;
    spk_names(k, "");
    ui.force = 1;
}
/* 0: playing; 1: the meta out of range (the slot stays silent) */
EXPORT uint32_t web_spk_commit(uint32_t k)
{
    uint32_t n = spk_meta[0], ns = spk_meta[2], i, base;
    int32_t root = (int32_t)spk_meta[1];
    k %= SPK_NU;
    web_spk_clear(k);
    if (!n || (n + 1u) / 2u > SPK_CAP || ns > SPK_NSLICE || root < 0 || root > 127 * 16)
        return 1;
    for (i = 0; i < ns; i++)
        if (!spk_meta[5 + 2 * i] || (spk_meta[4 + 2 * i] & 1u) ||
            spk_meta[4 + 2 * i] + (spk_meta[5 + 2 * i] + 1u) / 2u > SPK_CAP)
            return 1;
    spk_nm[sizeof spk_nm - 1] = 0;
    spk_names(k, spk_nm);
    base = (uint32_t)(uintptr_t)spk_pool[k] - (uint32_t)(uintptr_t)SMP_DATA;   /* (zone offsets count from SMP_DATA) */
    usr_zone[k][0] = (smp_zone_t){base, n, n - 1u, n - 1u, 32768u, (int16_t)root, 0, 0, 0, 127, 0};
    for (i = 0; i < ns; i++) {
        uint32_t off = spk_meta[4 + 2 * i], sn = spk_meta[5 + 2 * i], key = SPK_CHOP_KEY + i;
        usr_zone[SPK_NU + k][i] = (smp_zone_t){base + off, sn, sn - 1u, sn - 1u, 32768u, (int16_t)(key * 16u), 0, 0,
                                               (uint8_t)key, (uint8_t)key, 0};
        if (i < 8u) {
            spk_hit_d[k * 8u + i] = spk_pool[k] + off;
            spk_hit_n[k * 8u + i] = sn;
        }
    }
    usr_nz[SPK_NU + k] = (uint8_t)ns;
    usr_nz[k] = 1;
    return 0;
}
static void spk_boot(void)
{
    uint32_t k;
    for (k = 0; k < SPK_NU; k++)
        if (!usr_nz[k])
            spk_names(k, "");
}
EXPORT uint32_t web_spk_used(uint32_t k) { return usr_nz[k % SPK_NU]; }
/* the selected track plays user sample k: as 0 a synth (the whole, pitched), 1 chops (a slice a key from C3), 2 a kit
 * (slices 1..8 on the rows, every lane): the sound starts from UPRIGHT's (SAMPLE) / CRATE's (the kit) */
EXPORT void web_spk_use(uint32_t k, uint32_t mode)
{
    track_t *t = TSEL;
    uint32_t l;
    k %= SPK_NU;
    if (mode == 2u) {
        set_engine_of(t, ENGI_CKIT);
        apply_preset_to(t, 40);                      /* (CRATE: no fade, the hits as they are) */
        for (l = 0; l < 4u; l++)
            t->p[P_LN4 + l] = (int16_t)(CKM_N + FK_NSET + k);
    } else {
        set_engine_of(t, ENGI_SAMPLE);
        apply_preset_to(t, 9);                       /* (UPRIGHT: clean) */
        t->p[P_E0] = (int16_t)(SMP_NSETS + (mode ? SPK_NU : 0u) + k);
        t->p[P_E3] = 0;                              /* (no loop) */
        if (mode)
            t->p[P_REL] = 20;                        /* (a chop lets go quickly) */
    }
    ui.force = 1;
}

/* the user samples the song plays (the page's SHARE takes only these): bit k for slot k, from every track's sound and
 * the synth tracks' B sides (a pattern keeps only steps and timing, so the sounds are all here). SAMPLE's SET past the
 * built-in sets is slot k whole, then slot k's CHOP; the KIT's lane MODEL past the sampled sets is slot k's kit */
static uint32_t spk_of_sound(uint32_t eng, const int16_t *p)
{
    uint32_t m = 0, l;
    if (eng == ENGI_SAMPLE && p[P_E0] >= (int16_t)SMP_NSETS && p[P_E0] < (int16_t)(SMP_NSETS + 2u * SPK_NU))
        m |= 1u << ((uint32_t)(p[P_E0] - (int16_t)SMP_NSETS) % SPK_NU);
    if (eng == ENGI_CKIT)
        for (l = 0; l < 4u; l++)
            if (p[P_LN4 + l] >= (int16_t)(CKM_N + FK_NSET) && p[P_LN4 + l] < (int16_t)(CKM_N + FK_NSET + SPK_NU))
                m |= 1u << (uint32_t)(p[P_LN4 + l] - (int16_t)(CKM_N + FK_NSET));
    return m;
}
EXPORT uint32_t web_spk_in_use(void)
{
    uint32_t k, m = 0;
    for (k = 0; k < NTRK; k++)
        m |= spk_of_sound(trk[k].eng_req % NENGINES, trk[k].p);
    for (k = 0; k < B_N; k++)
        if (bsr[k].s.on == 1)                           /* (a B side on; 2: off, kept for later) */
            m |= spk_of_sound(bsr[k].eng_req % NENGINES, bsr[k].s.p) | spk_of_sound(bsr[k].s.engine % NENGINES, bsr[k].s.p);
    return m;
}

/* NEW SONG (the page's button; the device's SAVE > TOOLS > NEW + DEFAULT SOUNDS): every track cleared, the power-on
 * sounds; RANDOM SONG (MENU > SYSTEM > RANDOM SONG): a song rolled. The page has asked first, so no arming here; playing
 * stops first. Either can be undone: SAVE held */
EXPORT void web_new_song(uint32_t random)
{
    if (chain.running || chain.armed)
        chain_stop();
    if (song.playing)
        seq_stop();
    if (random) {
        rs_arm_t = fm1_ms | 1u;                    /* (boot_song.c: confirmed) */
        random_song();
    } else
        new_song(1);
    ui.force = 1;
}

/* the beginner's device: 1 the screen's words plain and the in-depth pages hidden (the page's default), 0 the device's
 * words and every page (POWER USER): gfx.c ui_plain, ui_simple */
EXPORT void web_plain(uint32_t on) { ui_plain = ui_simple = on ? 1u : 0u; ui.force = 1; }   /* (and the in-depth pages hidden) */
EXPORT void web_vapor(uint32_t k, uint32_t v) { vap_set(k & 3u, v); }

/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
EXPORT void web_render(uint32_t n)
{
    uint32_t k, slow = vap_get(0), wow = vap_get(1), vinyl = vap_get(2), crush = vap_get(3);
    if (n > 1024u)
        n = 1024u;
    if (!slow && !wow) {
        vap_pos = 0;
        while (ring_w - ring_r < n)
            web_ms();
        for (k = 0; k < n; k++) {
            uint32_t i = (ring_r++ % RING) * 2u;
            out_l[k] = ring[i];
            out_r[k] = ring[i + 1];
        }
    } else {                                           /* SLOWED, WOW: the device's frames read at a speed under 1 */
        double sp = vap_speed(), dw = wow * 0.009 / 127.0;
        for (k = 0; k < n; k++) {
            uint32_t a, b;
            float f;
            while (ring_w - ring_r < 2u)
                web_ms();
            a = (ring_r % RING) * 2u;
            b = ((ring_r + 1u) % RING) * 2u;
            f = (float)vap_pos;
            out_l[k] = ring[a] + (ring[b] - ring[a]) * f;
            out_r[k] = ring[a + 1] + (ring[b + 1] - ring[a + 1]) * f;
            vap_ph1 += 0.52 / FS;
            vap_ph2 += 6.1 / FS;
            vap_ph1 -= (double)(uint32_t)vap_ph1;
            vap_ph2 -= (double)(uint32_t)vap_ph2;
            vap_pos += sp * (1.0 + dw * (0.75 * sin_d(vap_ph1) + 0.25 * sin_d(vap_ph2)));
            while (vap_pos >= 1.0) {
                vap_pos -= 1.0;
                ring_r++;
            }
        }
    }
    if (crush) {                                       /* CRUSH: the rate held, then the bits */
        uint32_t hold = 1u + crush * 5u / 127u;
        float q = (float)(1u << (15u - crush * 10u / 127u));
        for (k = 0; k < n; k++) {
            if (vap_hc == 0u) {
                vap_hold_l = (float)(int32_t)(out_l[k] * q) / q;
                vap_hold_r = (float)(int32_t)(out_r[k] * q) / q;
            }
            vap_hc = vap_hc + 1u >= hold ? 0u : vap_hc + 1u;
            out_l[k] = vap_hold_l;
            out_r[k] = vap_hold_r;
        }
    }
    if (vinyl) {                                       /* VINYL: crackle (sparse clicks, a few ms each) and hiss */
        float amt = vinyl / 127.0f, p = amt * amt * 9.0f / (float)FS, hiss = amt * 0.006f;
        for (k = 0; k < n; k++) {
            float r = vap_rand(), c;
            if ((r * 0.5f + 0.5f) < p * 4.0f && vap_rand() > 0.5f)
                vap_crk = (0.05f + 0.25f * (vap_rand() * 0.5f + 0.5f)) * amt * (vap_rand() > 0 ? 1.0f : -1.0f);
            vap_crk *= 0.86f;                          /* (a click rings ~1 ms) */
            vap_crk_y += (vap_crk - vap_crk_y) * 0.45f;
            c = vap_crk_y;
            vap_hiss_y += (vap_rand() * hiss - vap_hiss_y) * 0.25f;
            out_l[k] = out_l[k] * (1.0f - amt * 0.06f) + c + vap_hiss_y;
            out_r[k] = out_r[k] * (1.0f - amt * 0.06f) + c * 0.85f + vap_hiss_y;
        }
    }
}

/* the flash: erased, then the page's saved sectors (web_nor), then boot */
EXPORT uint8_t *web_nor(void) { return nor; }
EXPORT uint32_t web_nor_size(void) { return sizeof nor; }
EXPORT void web_nor_erase(void) { memset(nor, 0xFF, sizeof nor); }
EXPORT uint32_t web_flash_writes_count(void) { return web_flash_writes; }
EXPORT void web_seed(uint32_t s) { web_seed_v = s; }   /* (before web_boot: the power-on song's seed) */
EXPORT void web_boot(void)
{
    if (!web_booted)
        web_power_on();
}

/* input, by the printed labels: buttons bit b = B_FX .. B_OCTUP (panel.c), keys bit k = key k (F3 .. G5),
 * encoders by role (EN_SELECT .. EN_K4), MASTER 0..1023 */
static uint32_t web_btn_labels, web_key_mask;
EXPORT void web_buttons(uint32_t labels)
{
    uint32_t m = 0, b, old = fm1_in.buttons;
    web_btn_labels = labels;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= 1u << panel.btn[b];
    web_pressed |= m & ~old;
    web_released |= old & ~m;
    fm1_in.buttons = m;
}
EXPORT void web_keys(uint32_t mask)
{
    uint32_t old = fm1_in.notes;
    mask &= (1u << 27) - 1u;
    web_key_mask = mask;
    web_notes_pressed |= mask & ~old;
    fm1_in.notes = mask;
}
EXPORT void web_enc(uint32_t role, int32_t n)
{
    if (role < NE)
        web_enc_steps[panel.enc[role] % 7u] += n * panel.dir[role];
}
EXPORT void web_master(uint32_t v) { web_master_adc = v > 1023u ? 1023u : v; }
/* a channel message from Web MIDI, as a USB-MIDI packet arrives (usb.c midi_enqueue): 0 = the ring was full */
EXPORT uint32_t web_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    if (status < 0x80u || status >= 0xF0u)
        return 1;
    return (uint32_t)midi_enqueue((status >> 4) | status << 8 | (d1 & 127u) << 16 | (d2 & 127u) << 24, 1);
}

/* the LEDs: bit b = the button labelled b lit (bit 14: PLAY's green), keys bit k; the dim glow and the breath alike */
static uint32_t led_on(const uint8_t *l, uint32_t id)
{
    uint32_t c, r;
    for (c = 0; c < FM1_NCOL; c++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][c] == (int8_t)id)
                return (l[c] >> r) & 1u;
    return 0;
}
static uint32_t led_buttons(const uint8_t *l)
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        m |= led_on(l, panel.btn[b]) << b;
    return m;
}
static uint32_t led_keys(const uint8_t *l)
{
    uint32_t k, m = 0;
    for (k = 0; k < 27u; k++)
        m |= led_on(l, 14u + k) << k;
    return m;
}
EXPORT uint32_t web_lit_buttons(void)
{
    return led_buttons(fm1_led) | ((fm1_led[LED_PLAY_GREEN >> 3] >> (LED_PLAY_GREEN & 7u)) & 1u) << 14;
}
EXPORT uint32_t web_lit_keys(void) { return led_keys(fm1_led); }
EXPORT uint32_t web_dim_buttons(void) { return led_buttons(fm1_led_dim); }
EXPORT uint32_t web_dim_keys(void) { return led_keys(fm1_led_dim); }
EXPORT uint32_t web_dim_level(void) { return web_dim_lo; }
EXPORT uint32_t web_breath_buttons(void) { return led_buttons(fm1_led_breath); }   /* #119: dark .. ~60 % of lit */
EXPORT uint32_t web_breath_keys(void) { return led_keys(fm1_led_breath); }
EXPORT uint32_t web_mid_keys(void) { return led_keys(fm1_led_mid); }   /* the DRUM grid's beats: a steady mid level */
/* the power-on sweep (or 1.2's idle animation) running: each LED's level now (hal/fm1_led_anim.h: 0..64 /64 of lit, | 0x80 the glow under
 * it), 0..13 the buttons by label, 14..40 the keys F3..G5; 0 when it is not */
EXPORT uint8_t *web_anim_levels(void)
{
    static uint8_t lv[FM1_ANIM_NBTN + FM1_ANIM_NKEY];
    uint32_t f = web_anim_frame(), i;
    if (!fm1_led_anim_on())
        return 0;
    for (i = 0; i < FM1_ANIM_NBTN + FM1_ANIM_NKEY; i++)
        lv[i] = (uint8_t)(web_anim_idle ? fm1_idle_level : fm1_anim_level)(f, i < FM1_ANIM_NBTN ? (i < NB ? panel.btn[i] : i) : i,
                                                                          web_anim_glow);
    return lv;
}

/* for the test and the bench (web/emu/emu_test.mjs), a heavy song straight into the tracks (as hostsim's
 * renders set them): T1 FM6 PAD, T2 PHYS DRONE STRING (SYMP), T3 GRAIN CLOUD PAD, 4-note chords on all 16
 * steps, held; T4 DRUM, kick snare and hats. PLAY is the page's (or the test's) to press */
EXPORT void web_test_heavy(void)
{
    static const uint8_t ENG[3][2] = {{12, 4}, {ENGI_PHYS, 7}, {8, 0}};
    static const uint8_t CH[4][4] = {{48, 55, 60, 64}, {45, 52, 57, 60}, {41, 48, 53, 57}, {43, 50, 55, 59}};
    uint32_t p, i;
    for (p = 0; p < 3u; p++) {
        track_t *t = &trk[p];
        host_preset(t, ENG[p][0], ENG[p][1]);
        t->p[P_VOICE] = V_POLY;
        t->p[P_SUS] = 127;
        for (i = 0; i < 16u; i++) {
            uint8_t n[4], k;
            for (k = 0; k < 4u; k++)
                n[k] = (uint8_t)(CH[i / 4u][k] + 12u * p);
            put_step(t, i, 4, n, ST_NOTE, 0);
        }
    }
    host_drums(&trk[3]);
    for (i = 0; i < 16u; i++) {
        uint8_t n[4];
        uint32_t k = 0;
        if (i % 4u == 0u)
            n[k++] = 36;
        if (i == 4u || i == 12u)
            n[k++] = 38;
        n[k++] = i % 2u ? 42 : 46;
        put_step(&trk[3], i, k, n, ST_NOTE, 0);
    }
    ui.force = 1;
}

EXPORT uint32_t web_test_user_preset(uint32_t i) { return (uint32_t)up_used(i); }

/* the sounds' audition (tests/spk_audition.mjs): part p takes engine e's factory preset k (e 255: as it is), then
 * note n on (vel > 0) or off (vel 0; n 0: none) */
EXPORT void web_test_note(uint32_t p, uint32_t e, uint32_t k, uint32_t n, uint32_t vel)
{
    track_t *t = &trk[p % NPART];
    if (e < NENGINES)
        host_preset(t, e, k);
    if (n && vel)
        trk_note_on(t, n & 127u, vel & 127u);
    else if (n)
        trk_note_off(t, n & 127u);
}

EXPORT uint16_t *web_screen(void) { return web_fb; }
EXPORT uint32_t web_screen_px(void) { return LCD_PX; }   /* the frame's side in pixels (480) */
EXPORT uint32_t web_screen_draws(void) { return web_draws; }
/* the changed rows since the last call, y0 | y1 << 16 (y1 exclusive; 0 nothing), then none */
EXPORT uint32_t web_screen_rows(void)
{
    uint32_t r = web_dy1 > web_dy0 ? web_dy0 | web_dy1 << 16 : 0u;
    web_dy0 = LCD_PX;
    web_dy1 = 0;
    return r;
}
EXPORT uint32_t web_now_ms(void) { return fm1_ms; }
EXPORT uint32_t web_playing(void) { return song.playing; }
EXPORT uint32_t web_sample_rate(void) { return FS; }

/* ------------------------------------------------------------------ the studio (index.html, bounce.js)
 * The page's recorder takes what the worklet plays. A bounce runs in a clone: the worklet's whole memory copied into a
 * second instance (in a worker), which then plays the song from the top faster than real time, nothing on its screen.
 * Between calls the clone's state is all in its memory (the stack pointer is back at its base), so the copy is exact. */

/* the music into the autosave now, as SAVE > PROJECT's QUICK (project.c quick_save) but silent: the page's project
 * export takes the flash after it. 0 saved, 1 playing (stop first), 2 failed */
EXPORT uint32_t web_save_now(void)
{
#if FELUCCA_FLASH
    uint32_t sig;
    if (transport_busy())
        return 1;
    if (!flash_ok)
        return 2;
    sig = autosave_sig();
    project_capture(&proj_scratch);
#if RONDO_LAYOUT
    proj_scratch.rsv = (uint8_t)(bank.cur + 1u);
    proj_scratch.sum = proj_sum(&proj_scratch);
#endif
    proj_wire_gen++;
    if (!proj_pack(&proj_wire, &proj_scratch) || st_save(OBJ_AUTOSAVE, &proj_wire, sizeof proj_wire) != 0)
        return 2;
    as.saved = as.seen = sig;
    as.last = fm1_ms;
    as.wrote = 1;
    as.err = 0;
    as.writes++;
    as.pend = 0;
    as.have = 1;
#if RONDO_LAYOUT
    {                                                     /* FREQ A..F too, as bank_poll stores them one by one */
        uint32_t p;
        fm1_irq_off();
        bank_keep();
        fm1_irq_on();
        for (p = 0; p < NBANK; p++) {
            bank_store_t *b = (bank_store_t *)(void *)&proj_wire;
            uint32_t len = 8u;
            if (!((bank.dirty >> p) & 1u))
                continue;
            proj_wire_gen++;
            memset(b, 0, sizeof *b);
            b->magic = BANK_MAGIC;
            b->pat = (uint8_t)p;
            b->ver = (uint8_t)(bank_has(p) ? BANK_VER : 0u);
            if (bank_has(p)) {
                uint32_t e = rle_enc((const uint8_t *)&bank_pat[p], sizeof bank_pat[p], b->enc, sizeof b->enc);
                if (!e)
                    return 2;
                b->len = (uint16_t)e;
                len = 8u + e;
            }
            if (st_save(OBJ_BANK0 + p, b, len) != 0)
                return 2;
            bank.dirty &= (uint8_t)~(1u << p);
            bk.writes++;
        }
    }
#endif
    return 0;
#else
    return 2;
#endif
}

/* a song to play (GLO > SONG has sections) */
EXPORT uint32_t web_song_sections(void) { return chain_valid(&chain_config) ? chain_config.count : 0u; }
EXPORT uint32_t web_bpm(void) { return (uint32_t)song.g[G_BPM]; }
/* one loop of the pattern: the longest FEED's LEN of steps at its DIV (and SWING), in samples */
EXPORT uint32_t web_pattern_frames(void)
{
    uint32_t k, i, best = 0;
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        uint32_t per = div_samples((uint32_t)t->p[P_SDIV]), n = 0, len = (uint32_t)t->p[P_SLEN];
        for (i = 0; i < len; i++)
            n += step_samples(t, per, i);
        if (n > best)
            best = n;
    }
    return (uint32_t)(best / vap_speed() + 0.5);       /* (in the page's frames: SLOWED stretches it) */
}

/* the clone: stop what plays, let it ring out (frames, the page's: the tails), the panel let go, no recording, the
 * internal clock; then (web_bounce_go) PLAY from the top: song 1 plays GLO > SONG once (the chain stops at its end:
 * web_playing 0), else the pattern; mute: bit k (NTRK order) leaves FEED k out (a stem: the others muted) */
EXPORT void web_bounce_stop(void)
{
    uint32_t k;
    web_headless = 1;
    web_buttons(0);
    web_keys(0);
    song.rec = 0;
    song.g[G_CLOCK] = 0;
    cin_bars = 0;
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_MUTE] = 0;
    if (song.playing || chain_busy() || transport_req)
        transport_req = 2;
}
EXPORT uint32_t web_bounce_go(uint32_t as_song, uint32_t mute)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_MUTE] = (int16_t)((mute >> k) & 1u);
    if (as_song) {
        uint32_t rc = chain_prepare();
        return rc;                                        /* 0 armed (PLAY follows), else project.c's reasons */
    }
    transport_req = 1;
    return 0;
}
EXPORT uint32_t web_ntrk(void) { return NTRK; }
/* FEED k's place on the panel (core.h fd_pos: DRUMS PERC SYNTH 1-3) */
EXPORT uint32_t web_feed_pos(uint32_t k) { return k < NTRK ? (uint32_t)fd_pos(k) : k; }
/* for the studio's test (web/emu/studio_test.mjs): MENU > SYSTEM > RANDOM SONG (a song in GLO > SONG) */
EXPORT void web_test_random_song(void) { random_song(); }
EXPORT uint32_t web_test_bank_used(void) { return bank.used; }

/* ------------------------------------------------------------------ the studio's share link (index.html)
 * A song as a link: the music's own storage objects, the autosave (the music as SAVE > PROJECT's QUICK holds it: the
 * FEEDs, their sounds, the song) and FREQ A..F, as stored (each already run-length coded), compressed by the page into
 * the link's #fragment. Opening one puts them into the visitor's flash before boot (the rest of it, their projects
 * and sounds, untouched) and boots restoring them. */
static uint8_t web_obj[ST_PAYLOAD_MAX];
EXPORT uint8_t *web_obj_buf(void) { return web_obj; }
EXPORT uint32_t web_obj_max(void) { return ST_PAYLOAD_MAX; }
EXPORT uint32_t web_obj_autosave(void) { return OBJ_AUTOSAVE; }
EXPORT uint32_t web_obj_bank0(void) { return OBJ_BANK0; }
EXPORT uint32_t web_obj_count(void) { return OBJ_COUNT; }
/* the object's current copy into web_obj: its length, or -1 (none) */
EXPORT int32_t web_obj_get(uint32_t obj) { return obj < OBJ_COUNT ? st_load(obj, web_obj, sizeof web_obj) : -1; }
/* web_obj's first len bytes as the object (before web_boot: the flash as restored); 0 written */
EXPORT int32_t web_obj_put(uint32_t obj, uint32_t len)
{
    if (obj != OBJ_AUTOSAVE && (obj < OBJ_BANK0 || obj >= OBJ_COUNT))
        return -1;                                        /* the music's objects only: never the settings or presets */
    if (obj == OBJ_AUTOSAVE)
        web_shared = 1;
    return st_save(obj, web_obj, len);
}

/* ------------------------------------------------------------------ the Spelunkr page (index.html: the Guide)
 * The page draws the song as a TV guide and its own controls around the screen; it reads the device's state as JSON
 * (web_guide, a few times a second) and acts through the calls below, between the worklet's blocks. */
static char web_js[6144];
static uint32_t web_jn;
static void wj(const char *fmt, ...)
{
    va_list ap;
    int n;
    if (web_jn >= sizeof web_js - 1u)
        return;
    va_start(ap, fmt);
    n = vsnprintf(web_js + web_jn, sizeof web_js - web_jn, fmt, ap);
    va_end(ap);
    if (n > 0)
        web_jn += (uint32_t)n < sizeof web_js - web_jn ? (uint32_t)n : sizeof web_js - 1u - web_jn;
}
static void wj_str(const char *s)                 /* a JSON string: the names are ASCII, quotes and backslashes escaped */
{
    wj("\"");
    for (; *s; s++)
        wj(*s == '"' || *s == '\\' ? "\\%c" : (unsigned char)*s < 32u ? "?" : "%c", *s);   /* (UTF-8 as is) */
    wj("\"");
}
/* track k's pattern p: the share of its steps that play (0..100), -1 no such pattern */
static int32_t web_density(uint32_t k, uint32_t p)
{
    const step_t *st;
    uint32_t i, len, on = 0;
    if (p == bank.cur) {
        st = trk[k].step;
        len = (uint32_t)trk[k].p[P_SLEN];
    } else if (bank_has(p)) {
        st = bank_pat[p].step[k];
        len = (uint32_t)bank_pat[p].timing[k][0];
    } else
        return -1;
    if (!len || len > NSTEP)
        len = NSTEP;
    for (i = 0; i < len; i++)
        on += st[i].time == ST_NOTE && (st[i].n || st[i].hit);
    return (int32_t)(on * 100u / len);
}
static void web_buttons_json(void);
EXPORT const char *web_guide(void)
{
    uint32_t k, p, r;
    web_jn = 0;
    wj("{\"bpm\":%d,\"play\":%d,\"rec\":%d,\"sel\":%u,\"cur\":%u,\"queue\":%d,\"used\":%u,\"oct\":%d,", song.g[G_BPM],
       song.playing ? 1 : 0, song.rec != 0, (unsigned)song.sel, (unsigned)bank.cur, bank.queue < NBANK ? (int)bank.queue : -1,
       (unsigned)bank.used, song.octave);
    wj("\"song\":{\"running\":%d,\"row\":%u,\"rows\":[", chain.running ? 1 : 0, (unsigned)chain.row);
    for (r = 0; chain_valid(&chain_config) && r < chain_config.count && r < CHAIN_ROWS; r++) {
        wj(r ? ",[%u" : "[%u", (unsigned)chain_config.row[r].repeat);
        for (k = 0; k < NTRK; k++) {
            uint32_t q = chain_pat(chain_config.row[r].slot[k]);
            wj(",%d", q == CHAIN_OFF ? -1 : (int)q);
        }
        wj("]");
    }
    wj("]},\"tracks\":[");
    for (k = 0; k < NTRK; k++) {
        char nm[20];
        const track_t *t = &trk[k];
        sound_name(t, nm);
        wj(k ? ",{" : "{");
        wj("\"pos\":%u,\"sound\":", (unsigned)fd_pos(k));
        wj_str(nm);
        wj(",\"engine\":");
        wj_str(ENGINES[t->eng_req % NENGINES]->name);
        wj(",\"mute\":%d,\"solo\":%d,\"b\":%d,\"step\":%u,\"len\":%d,\"vox\":%u,\"pats\":[", t->p[P_MUTE] ? 1 : 0,
           (web_solo >> k) & 1u, k < NTRK && b_on(t) ? 1 : 0, (unsigned)t->seq_idx, t->p[P_SLEN], (unsigned)trk_cap(k));
        for (p = 0; p < NBANK; p++)
            wj(p ? ",%d" : "%d", (int)web_density(k, p));
        wj("]}");
    }
    wj("],\"knobs\":[");
    for (k = 0; k < 4u; k++) {                          /* KNOB 1..4: what their cards on the screen say */
        int on = (web_cards_shown >> k) & 1u;
        char full[28];
        snprintf(full, sizeof full, "%s%s%s", on ? web_card_now[k].val : "", on && web_card_now[k].unit[0] ? " " : "",
                 on ? web_card_now[k].unit : "");
        wj(k ? ",{\"label\":" : "{\"label\":");
        wj_str(on ? web_card_now[k].label : "");
        wj(",\"val\":");
        wj_str(full);
        wj(",\"frac\":%d}", on && web_card_now[k].ratio >= 0 ? web_card_now[k].ratio : -1);
    }
    wj("],\"xy\":[%u,%u,%u,%u,%u,%u]", xy_live[0].x, xy_live[0].y, xy_live[0].on, xy_live[1].x, xy_live[1].y, xy_live[1].on);
    wj(",\"vapor\":[%u,%u,%u,%u]", vap_get(0), vap_get(1), vap_get(2), vap_get(3));
    web_buttons_json();
    wj("}");
    web_js[web_jn] = 0;
    return web_js;
}
EXPORT uint32_t web_guide_len(void) { return web_jn; }
/* the page's controls */
EXPORT void web_track(uint32_t k) { if (k < NTRK && k != song.sel) { track_select(k); ui.force = 1; } }
EXPORT void web_mute(uint32_t k) { if (k < NTRK) { trk[k].p[P_MUTE] = (int16_t)!trk[k].p[P_MUTE]; ui.force = 1; } }
EXPORT void web_solo_toggle(uint32_t k) { if (k < NTRK) { web_solo ^= (unsigned char)(1u << k); ui.force = 1; } }
EXPORT uint32_t web_cue(uint32_t p) { uint32_t rc = p < NBANK ? (uint32_t)bank_key(p) : 1u; ui.force = 1; return rc; }
/* the FX PAD under the mouse: crosshair j at x y (0..127), on; on 0: let go (it springs back), and the pad closes
 * once both are let go */
EXPORT void web_xy(uint32_t j, uint32_t x, uint32_t y, uint32_t on)
{
    if (j > 1u)
        return;
    web_pad = 1;
    xy_open(song.sel % NTRK);
    xy_live[j].x = (uint8_t)(on ? (x > 127u ? 127u : x) : 64u);
    xy_live[j].y = (uint8_t)(on ? (y > 127u ? 127u : y) : 64u);
    xy_live[j].on = (uint8_t)(on != 0);
    if (!xy_live[0].on && !xy_live[1].on)
        web_pad = 0;
}

/* ------------------------------------------------------------------ the Spelunkr page: what each button does now
 * The face buttons change job with the page (EDIT clears a step on the GRID, renames a slot on SAVE ..): the page
 * labels each by what a tap does here and now, in plain words, and says what holding it does. */
static const char *const WEB_FAM[FAM_COUNT] = {"HOME", "ENVELOPE", "WOBBLE", "EFFECTS", "SCALE", "SOUND", "MIX", "SAVE",
                                               "ARP", "STEPS", "MIXER"};
/* a page's plain name */
static const char *web_page_name(uint32_t i)
{
    static const char *const MAP[][2] = {
        {"ENV", "ENVELOPE"}, {"ENV DEST", "ENV TARGETS"}, {"LFO", "WOBBLE"}, {"LFO 2", "WOBBLE 2"}, {"LFO DEST", "WOBBLE TARGETS"},
        {"MOD", "MOD MATRIX"}, {"FX", "SENDS"}, {"MASTER", "MASTER FX"}, {"DLY", "DELAY"}, {"SCL", "SCALE"},
        {"EDIT 1", "SOUND 1"}, {"EDIT 2", "SOUND 2"}, {"ORBIT", "SONG"}, {"USER", "SAVE SOUND"}, {"PRESETS", "BROWSE"},
        {"PROJECT", "SAVE SONG"}, {"GRID", "STEPS"}, {"CLOCK", "TEMPO"}};
    uint32_t k;
    const char *t = i < NPAGES ? PAGES[i].title : "";
    for (k = 0; k < sizeof MAP / sizeof MAP[0]; k++)
        if (!strcmp(t, MAP[k][0]))
            return MAP[k][1];
    return t;
}
/* the page a family's button opens from here (ui.c open_family, without opening it) */
static uint32_t web_fam_target(uint32_t fam)
{
    uint32_t n, i;
    if (!ui.home && cur_page()->fam == fam) {
        for (n = 0, i = ui.page; n < NPAGES; n++) {
            i = (i + 1u) % NPAGES;
            if (PAGES[i].fam == fam && page_visible(i))
                return i;
        }
        return ui.page;
    }
    if (fam == FAM_SAVE) {
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].graph == GR_USER)
                return i;
    }
    i = ui.fam_last[fam];
    return i && i < NPAGES && PAGES[i].fam == fam && page_visible(i) ? i : page_first(fam);
}
static const uint8_t WEB_BTN_FAM[14] = {FAM_FX, FAM_SCL, FAM_ENV, FAM_LFO, FAM_EDIT, FAM_GLO, FAM_HOME, FAM_SAVE, FAM_ARP,
                                        FAM_SEQ, 0xFF, 0xFF, 0xFF, 0xFF};   /* by panel.c B_* (FX SCL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+) */
static const char *const WEB_HOLD[14] = {"hold + keys: glitch effects", "hold + keys: pick a scale", "", "hold + knobs: try a change, let go to undo",
                                         "hold + keys: pick a synth engine", "hold + keys: mute and solo", "hold: settings menu",
                                         "hold: undo", "hold + knobs: ride an effect", "hold + keys: step tools", "", "hold: recording tools", "", ""};
static void web_btn(uint32_t b, char *act, char *sub)
{
    const page_t *pg = cur_page();
    int page = !ui.home && !ui.menu;
    uint32_t f = WEB_BTN_FAM[b];
    act[0] = sub[0] = 0;
    str_cpy(sub, WEB_HOLD[b], 48);
    /* the dialogs and pickers first: there OCT- / OCT+ say no / yes */
    if (b == B_OCTDN || b == B_OCTUP) {
        int up = b == B_OCTUP;
        if (name_on() && !ui.confirm && !ui.menu) { str_cpy(act, up ? "SAVE NAME" : "CANCEL", 24); return; }
        if (ui.confirm) { str_cpy(act, up ? "YES" : "NO", 24); return; }
        if (ui.menu) { str_cpy(act, ui.menu >= 2u ? (up ? "—" : "BACK") : up ? "VALUE +" : "VALUE −", 24); return; }
        if (layer_set_open()) { str_cpy(act, up ? "—" : "UNDO", 24); return; }
        if (act_cols()) { str_cpy(act, up ? "DO IT" : "BACK", 24); return; }
        if (xy_trk < NTRK && page && !up) { str_cpy(act, "CLEAR PRINTS", 24); return; }
        if (page && bank_keys_on()) { str_cpy(act, up ? "COPY" : "CLEAR", 24); str_cpy(sub, up ? "hold + a pattern key" : "hold + a pattern key; tap: stay", 48); return; }
        if ((page && pg->graph == GR_TRK) || (ui.home && home_tracks())) { str_cpy(act, up ? "PAGE ›" : "‹ PAGE", 24); return; }
        str_cpy(act, up ? "OCTAVE +" : "OCTAVE −", 24);
        str_cpy(sub, "both: back to the middle", 48);
        return;
    }
    if (ui.menu && b == B_HOME) { str_cpy(act, "CLOSE MENU", 24); return; }
    if (b == B_PLAY) {
        str_cpy(act, song.playing || chain_busy() || seq_counting() ? "STOP" : page && pg->graph == GR_SONG ? "PLAY SONG" : "PLAY", 24);
        str_cpy(sub, ui.layer == LAYER_GLO ? "from the top" : "", 48);
        return;
    }
    if (b == B_REC) { str_cpy(act, song.rec ? "REC OFF" : "RECORD", 24); return; }
    if (b == B_EDIT) {                                   /* (ui_input.c page_tap's order) */
        if (detail_on()) { str_cpy(act, "RESET STEP", 24); return; }
        if (song.seq_mode && page && pg->graph == GR_ROLL) { str_cpy(act, lock_held() ? "CLEAR LOCKS" : "CLEAR STEP", 24); return; }
        if (page && pg->graph == GR_EVENTS) { str_cpy(act, "DELETE", 24); return; }
        if (page && (pg->graph == GR_USER || pg->graph == GR_SLOTS)) { str_cpy(act, "RENAME", 24); return; }
    }
    if (b == B_HOME) {
        if (ui.home) { str_cpy(act, "MIXER", 24); str_cpy(sub, "hold: settings menu", 48); return; }
        if (page && pg->fam == FAM_TRK && ui.page + 1u < NPAGES && PAGES[ui.page + 1u].fam == FAM_TRK) { str_cpy(act, web_page_name(ui.page + 1u), 24); return; }
        str_cpy(act, "HOME", 24);
        return;
    }
    if (f < FAM_COUNT) {
        uint32_t t = web_fam_target(f);
        if (page && pg->fam == f && t != ui.page) {      /* its own page: the next one of the family */
            str_cpy(act, web_page_name(t), 24);
            str_cpy(act + str_len(act), " ›", 24 - str_len(act));
        } else if (page && pg->fam == f) {
            str_cpy(act, WEB_FAM[f], 24);                /* (a family of one page: it stays) */
        } else {
            str_cpy(act, WEB_FAM[f], 24);
        }
    }
}
/* "page": the page's plain name, "btn": [[act, sub] x 14] (appended to web_guide's JSON) */
static void web_buttons_json(void)
{
    uint32_t b;
    wj(",\"page\":");
    wj_str(ui.menu ? "MENU" : ui.home ? "HOME" : web_page_name(ui.page));
    wj(",\"b\":[");
    for (b = 0; b < 14u; b++) {
        char act[24], sub[48];
        web_btn(b, act, sub);
        wj(b ? ",[" : "[");
        wj_str(act);
        wj(",");
        wj_str(sub);
        wj("]");
    }
    wj("]");
}
