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
static unsigned char web_pad_k;          /* the track the page's pads are on (Ed, Nov 22: a locked pad stays on its track) */
#define WEB_PAD_TRK web_pad_k
static uint32_t web_fx_keys;           /* (Nov 25) 1: FX tapped locks the FX keys open (JAM PADS, the box: ui_layer.c layer_tap);
                                         * 0: FX opens the effects pages, as on the FM-1 (web_fx_mode) */
#define WEB_FX_LOCK web_fx_keys
#define WEB_FX_NODTAP (!web_fx_keys)          /* (EFFECTS: two quick taps are two pages, not the FX keys locked) */
static unsigned char web_rec_ask;        /* REC tapped while stopped: the page asks about the metronome (web_rec_go) */
#define WEB_REC_ASK web_rec_ask
static int16_t web_route_was = -1;     /* the song's ROUT before a keyboard's note set it to SEL (web_midi) */
static unsigned char web_midi_sel = 1;   /* a MIDI keyboard plays the picked track (ROUT SEL), whatever its channel; 0:
                                         * the song's own ROUT (by channel: CH1-4 ..), the page's MIDI choice */
static unsigned char web_click_held, web_click_mode, web_cin_bars, web_rec_ran;   /* (ui.c rp_apply: over the settings) */
#define WEB_CLICK_HELD web_click_held
#define WEB_CLICK_MODE web_click_mode
#define WEB_CIN_BARS web_cin_bars
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
static void web_pol_comp(uint32_t k, uint32_t part, int32_t *b, int32_t *d, uint32_t n);   /* POLISH's compressor (below) */
static uint32_t web_pol_part(uint32_t k, uint32_t s0, uint32_t kit);
static void web_pol_track(uint32_t k, int32_t *b, int32_t *d, uint32_t n);
#define POLISH_TRACK(k, b, d, n) web_pol_track((k), (b), (d), (n))
#define POLISH_VOICE_ON(t, v) web_pol_part((uint32_t)((t) - trk), (uint32_t)(v)->s[0], (uint32_t)ENG_KIT((t)->engine % NENGINES))
#define POLISH_VOICE(t, v, b, n) web_pol_comp((uint32_t)((t) - trk), web_pol_part((uint32_t)((t) - trk), (uint32_t)(v)->s[0], 1u), (b), 0, (n))
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
static void web_touch_btn(uint32_t b, int on);
#define TOUCH_BTN(b, on) web_touch_btn((b), (on))
static void web_touch_key(uint32_t k, int on);
#define TOUCH_KEY(k, on) web_touch_key((k), (on))
#include "../../firmware/src/ui_touch.c"            /* the screen as a touch surface (web_touch) */

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
#include "../../firmware/src/lead.c"           /* the Spelunkr Groovebox: LEAD, PHANTOM recorded and with more sounds */

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
static int32_t web_batt_adc = 620;     /* the battery as the ADC reads it: full (the page), the box's cells (web_batt) */
static int32_t fm1_adc_read(uint32_t ch) { return ch == FM1_ADC_MASTER ? (int32_t)web_master_adc : web_batt_adc; }
/* the Spelunkr Box's cells (its Pico, CC 22): 1..127 the charge, 0 on DC (shown full). The screen's bars are at 531 /
 * 561 / 591 (ui_draw.c): 1..127 spread over 516..620, so a third of the charge a bar (web_batt, below) */
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
        lead_boot();                      /* LEAD's sound and lanes (lead.c) */
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
    if (!web_headless)
        lead_poll();
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
static void web_touch_tick(void);
static void web_ms(void)
{
    fm1_ms++;
    web_touch_tick();
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
    if (web_rec_ask && (song.playing || seq_counting() || !song.rec))
        web_rec_ask = 0;                                  /* (started or disarmed some other way: no question) */
    if (web_click_held) {                                 /* web_rec_go's metronome: the settings' again once the take is
                                                           * over (stopped, or REC let go) */
        if (song.playing || seq_counting())
            web_rec_ran = 1;
        if ((web_rec_ran && !song.playing && !seq_counting()) || (web_rec_ran && !song.rec) || (!web_rec_ran && !song.rec))
            web_click_held = 0;
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
EXPORT void web_batt(uint32_t level) { web_batt_adc = level ? 515 + (int32_t)(level > 127u ? 127u : level) * 105 / 127 : 620; }
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
static double vap_speed(void) { return 1.0 - vap_get(0) * 0.5 / 127.0; }   /* (Nov 26, Ed: down to half speed; was 0.7) */
EXPORT uint32_t web_vapor_get(uint32_t k) { return vap_get(k & 3u); }

/* ---- Spelunkr: POLISH (Ed, Nov 25 2026: "some mastering options, maybe the option to put compression on one of the
 * tracks, a simple 3 channel EQ and an exciter with a handful of options for each"). The page's, not the song's
 * (index.html keeps them: this browser, SAVE's file, a song code), set here by web_polish(id, v):
 *   COMPRESS  on one track (k: NTRK order, NTRK = LEAD), in the device after its FX PAD and before its level (core.h
 *             POLISH_TRACK): STYLE GENTLE PUNCH SQUASH PUMP (ratio, attack, release, knee), AMOUNT (the threshold, 0
 *             .. -32 dB of a voice's full scale; make-up: as loud as it came in over ~2 s (up to
 *             +24 dB), so AMOUNT changes the feel, not the loudness), MIX (parallel: dry under it)
 *   EQ        on the master after VAPOR: LOW shelf 110 Hz, MID bell (MID AT 250 700 1.5k 3.5k Hz), HIGH shelf 7.5 kHz,
 *             -12 .. +12 dB each
 *   EXCITE    after the EQ: a band of the mix driven into a curve, only the harmonics it makes added back: AIR (over 5
 *             kHz, odd), SHINE (over 2.5 kHz, even), WARM (the mids, tube-like), BASS (the lows' harmonics, heard on
 *             small speakers); AMOUNT
 * With EQ or EXCITE on, a peak limiter at the end (-0.2 dB) catches what a boost pushes over. All off: nothing runs */
enum { PO_CON, PO_CTRK, PO_CSTY, PO_CAMT, PO_CMIX, PO_EON, PO_ELO, PO_EMID, PO_EHI, PO_EMF, PO_XON, PO_XMODE, PO_XAMT,
       PO_CPART, PO_SON, PO_SMASK, PO_SFEEL, PO_SAMT, PO_N };   /* (SIDECHAIN appended, Nov 25) */                   /* (CPART appended, Nov 25: 0 the whole track; on DRUMS 1 KICK, 2 SNARE; on PERC
                                            * 1 PERC 1, 2 PERC 2: core.h POLISH_VOICE) */
static int32_t pol[PO_N] = {0, 0, 0, 64, 127, 0, 0, 0, 0, 1, 0, 0, 48, 0, 0, 14, 1, 80};
static const int32_t POL_MAX[PO_N] = {1, NTRK, 3, 127, 127, 1, 12, 12, 12, 3, 1, 3, 127, 2, 1, (1 << (NTRK + 1)) - 2, 3, 127};
/* the part the compressor works on: 0 the whole track (CPART 0, not a kit track, or DRUMS playing another engine) */
static uint32_t pol_part_now(void)
{
    uint32_t k = (uint32_t)pol[PO_CTRK];
    if (!pol[PO_CPART] || !trk_is_kit(k) || !ENG_KIT(trk[k].engine % NENGINES))
        return 0;
    return (uint32_t)pol[PO_CPART];
}
/* a kit voice's part (1 KICK / PERC 1, 2 SNARE / PERC 2) when the compressor wants it on its own, else 0: on DRUMS
 * s[0] is the voice's lane (eng_cycle.c CK_KICK 0, CK_SNARE 1), on PERC its slot in bits 0-1 */
static uint32_t web_pol_part(uint32_t k, uint32_t s0, uint32_t kit)
{
    uint32_t want;
    if (!pol[PO_CON] || !pol[PO_CAMT] || k != (uint32_t)pol[PO_CTRK] || !kit || !(want = pol_part_now()))
        return 0;
    return (k == PERC_TRK ? (s0 & 3u) : s0) + 1u == want ? want : 0;
}
static const struct { float ratio, att, rel, knee; } PC_STY[4] = {
    {2.0f, 40.0f, 400.0f, 10.0f},          /* GENTLE: glue, barely there */
    {4.0f, 45.0f, 220.0f, 6.0f},           /* PUNCH: the attack let through, the body held */
    {10.0f, 3.0f, 90.0f, 3.0f},            /* SQUASH: flat and loud */
    {6.0f, 12.0f, 500.0f, 4.0f},           /* PUMP: a long release that breathes */
};
static float pc_env, pc_g, pc_grmin;       /* the detector, the gain reduction now (dB, <= 0), the deepest since the
                                             * guide's last look */
static float pc_pin, pc_pout, pc_mk = 1.0f; /* the make-up: the track's power in and out over ~2 s, its gain */
static uint32_t pc_last_ms, pc_k_was = 255u;   /* (the target: track x 4 + part) */
static float pol_ms(float ms) { return ms <= 0 ? 1.0f : 1.0f - expf(-1000.0f / (ms * (float)FS)); }
static void web_pol_comp(uint32_t k, uint32_t part, int32_t *b, int32_t *d, uint32_t n)
{
    float thr, R, W, ka, kr, gr2, ks, mix;
    uint32_t i, sty, tg = k * 4u + part;
    if (!pol[PO_CON] || k != (uint32_t)pol[PO_CTRK] || !pol[PO_CAMT] || part != pol_part_now())
        return;                                         /* (a part on its own: the track's own call passes it by) */
    sty = (uint32_t)pol[PO_CSTY] & 3u;
    if (tg != pc_k_was) {                               /* a new target: from rest */
        pc_env = pc_g = 0;
        pc_pin = pc_pout = 0;
        pc_mk = 1.0f;
    } else if (fm1_ms - pc_last_ms > 3u) {              /* silent a while (a drum between hits): let go as it would have */
        pc_env = 0;
        pc_g *= expf(-(float)(fm1_ms - pc_last_ms) / PC_STY[sty].rel);
    }
    pc_k_was = tg;
    pc_last_ms = fm1_ms;
    R = PC_STY[sty].ratio;
    W = PC_STY[sty].knee;
    thr = -32.0f * (float)pol[PO_CAMT] / 127.0f;
    ks = pol_ms(2000.0f);
    ka = pol_ms(PC_STY[sty].att);
    kr = pol_ms(PC_STY[sty].rel);
    gr2 = pol_ms(15.0f);                                  /* (the detector lets go quickly: the STYLE's release rules) */
    mix = (float)pol[PO_CMIX] / 127.0f;
    for (i = 0; i < n; i++) {
        float x = (float)b[i] * (1.0f / 32768.0f), a = fabsf(x), lv, ov, g, y;
        if (d)
            a += fabsf((float)d[i] * (1.0f / 32768.0f));
        pc_env = a > pc_env ? a : pc_env + (a - pc_env) * gr2;   /* (peak: at once up, ~15 ms down) */
        lv = 20.0f * log10f(pc_env + 1e-6f);
        ov = lv - thr;
        if (2.0f * ov < -W)
            g = 0;
        else if (2.0f * fabsf(ov) <= W)
            g = (1.0f / R - 1.0f) * (ov + W * 0.5f) * (ov + W * 0.5f) / (2.0f * W);
        else
            g = (1.0f / R - 1.0f) * ov;
        pc_g += (g - pc_g) * (g < pc_g ? ka : kr);     /* (Ed, Nov 25: "loosen it up": every STYLE's attack and release longer) */
        if (pc_g < pc_grmin)
            pc_grmin = pc_g;
        y = powf(10.0f, pc_g * 0.05f);
        pc_pin += (x * x - pc_pin) * ks;               /* make-up: as loud as it came in, over ~2 s (AMOUNT changes the
                                                         * feel, not the loudness) */
        pc_pout += (x * x * y * y - pc_pout) * ks;
        if (pc_pout > 1e-9f) {
            float w = sqrtf(pc_pin / pc_pout);
            pc_mk += ((w > 16.0f ? 16.0f : w < 1.0f ? 1.0f : w) - pc_mk) * ks;
        }
        y *= pc_mk;
        y = mix * y + (1.0f - mix);
        b[i] = (int32_t)((float)b[i] * y);
        if (d)
            d[i] = (int32_t)((float)d[i] * y);
    }
}

/* a biquad (RBJ), one channel's state */
typedef struct { float b0, b1, b2, a1, a2; } bq_t;
typedef struct { float x1, x2, y1, y2; } bqs_t;
static inline float bq_run(const bq_t *q, bqs_t *s, float x)
{
    float y = q->b0 * x + q->b1 * s->x1 + q->b2 * s->x2 - q->a1 * s->y1 - q->a2 * s->y2;
    s->x2 = s->x1; s->x1 = x; s->y2 = s->y1; s->y1 = y;
    return y;
}
enum { BQ_LP, BQ_HP, BQ_PEAK, BQ_LSH, BQ_HSH };
static void bq_make(bq_t *q, int type, float f, float Q, float db)
{
    float A = powf(10.0f, db / 40.0f), w = 6.2831853f * f / (float)FS, c = cosf(w), sn = sinf(w), al = sn / (2.0f * Q);
    float b0, b1, b2, a0, a1, a2, sq = 2.0f * sqrtf(A) * al;
    switch (type) {
    case BQ_LP: b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_HP: b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; break;
    case BQ_PEAK: b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; break;
    case BQ_LSH:
        b0 = A * ((A + 1) - (A - 1) * c + sq); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - sq);
        a0 = (A + 1) + (A - 1) * c + sq; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - sq; break;
    default:
        b0 = A * ((A + 1) + (A - 1) * c + sq); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - sq);
        a0 = (A + 1) - (A - 1) * c + sq; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - sq; break;
    }
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}
static const float EQ_MIDF[4] = {250.0f, 700.0f, 1500.0f, 3500.0f};
static bq_t eq_q[3];
static bqs_t eq_s[3][2];
static int32_t eq_key = -1;
/* EXCITE: per mode the band in (HP, LP; 0 none), the curve's drive and lean (even harmonics), the band out (HP, LP),
 * how much of what it makes goes back */
static const struct { float hp, lp, drive, fund, lean, ohp, olp, gain; } XM[4] = {   /* (fund: the curve's own
                                                         * fundamental at that drive, taken back out) */
    {5000.0f, 0.0f, 4.0f, 1.24f, 0.0f, 5000.0f, 0.0f, 1.0f},          /* AIR */
    {2500.0f, 0.0f, 3.0f, 1.21f, 0.5f, 2500.0f, 0.0f, 0.9f},          /* SHINE */
    {180.0f, 1600.0f, 1.8f, 1.08f, 0.9f, 300.0f, 5000.0f, 1.0f},      /* WARM */
    {0.0f, 120.0f, 1.6f, 1.04f, 1.0f, 110.0f, 450.0f, 1.6f},          /* BASS */
};
static bq_t xq[4];                          /* in HP, in LP, out HP, out LP */
static bqs_t xs[4][2];
static int32_t xq_mode = -1;
static float lim_g = 1.0f;
static uint32_t pol_was_on;
static float xenv[2];                      /* EXCITE: the band's level, per side (the curve sees it at ~1) */
static inline float xcurve(float v, float lean) { float t = tanhf(v); return t + lean * (t * t - 0.5f); }
static void pol_master(uint32_t n)
{
    uint32_t i, j, eq = pol[PO_EON] && (pol[PO_ELO] | pol[PO_EMID] | pol[PO_EHI]), xc = pol[PO_XON] && pol[PO_XAMT];
    if (!eq && !xc) {
        if (pol_was_on) {                               /* (off: from rest next time) */
            memset(eq_s, 0, sizeof eq_s);
            memset(xs, 0, sizeof xs);
            lim_g = 1.0f;
            pol_was_on = 0;
        }
        return;
    }
    pol_was_on = 1;
    if (eq) {
        int32_t key = (pol[PO_ELO] + 12) | (pol[PO_EMID] + 12) << 5 | (pol[PO_EHI] + 12) << 10 | pol[PO_EMF] << 15;
        if (key != eq_key) {
            bq_make(&eq_q[0], BQ_LSH, 110.0f, 0.707f, (float)pol[PO_ELO]);
            bq_make(&eq_q[1], BQ_PEAK, EQ_MIDF[pol[PO_EMF] & 3], 0.9f, (float)pol[PO_EMID]);
            bq_make(&eq_q[2], BQ_HSH, 7500.0f, 0.707f, (float)pol[PO_EHI]);
            eq_key = key;
        }
        for (i = 0; i < n; i++)
            for (j = 0; j < 3u; j++) {
                out_l[i] = bq_run(&eq_q[j], &eq_s[j][0], out_l[i]);
                out_r[i] = bq_run(&eq_q[j], &eq_s[j][1], out_r[i]);
            }
    }
    if (xc) {
        uint32_t m = (uint32_t)pol[PO_XMODE] & 3u;
        float amt = (float)pol[PO_XAMT] / 127.0f, g = XM[m].gain * amt, dr = XM[m].drive, xatt = pol_ms(1.0f), xrel = pol_ms(40.0f);
        if ((int32_t)m != xq_mode) {
            if (XM[m].hp > 0) bq_make(&xq[0], BQ_HP, XM[m].hp, 0.707f, 0);
            if (XM[m].lp > 0) bq_make(&xq[1], BQ_LP, XM[m].lp, 0.707f, 0);
            if (XM[m].ohp > 0) bq_make(&xq[2], BQ_HP, XM[m].ohp, 0.707f, 0);
            if (XM[m].olp > 0) bq_make(&xq[3], BQ_LP, XM[m].olp, 0.707f, 0);
            memset(xs, 0, sizeof xs);
            xenv[0] = xenv[1] = 0;
            xq_mode = (int32_t)m;
        }
        for (i = 0; i < n; i++) {
            float *o[2] = {&out_l[i], &out_r[i]};
            for (j = 0; j < 2u; j++) {
                float v = *o[j], h;
                if (XM[m].hp > 0) v = bq_run(&xq[0], &xs[0][j], v);
                if (XM[m].lp > 0) v = bq_run(&xq[1], &xs[1][j], v);
                {                                 /* the band at ~1 (its level followed), driven, only what the curve
                                                         * made kept, back at the band's level: the same colour loud or soft */
                    float a = fabsf(v), u;
                    xenv[j] += (a - xenv[j]) * (a > xenv[j] ? xatt : xrel);
                    u = v / (xenv[j] + 1e-4f);
                    h = (xcurve(u * dr, XM[m].lean) - XM[m].fund * u) * 0.5f * xenv[j];
                }
                if (XM[m].ohp > 0) h = bq_run(&xq[2], &xs[2][j], h);
                if (XM[m].olp > 0) h = bq_run(&xq[3], &xs[3][j], h);
                *o[j] += h * g * 2.0f;
            }
        }
    }
    {                                                   /* the limiter: instant down, ~120 ms back */
        float rel = pol_ms(120.0f);
        for (i = 0; i < n; i++) {
            float a = fabsf(out_l[i]) > fabsf(out_r[i]) ? fabsf(out_l[i]) : fabsf(out_r[i]), want = a * lim_g > 0.977f ? 0.977f / a : 1.0f;
            if (want < lim_g)
                lim_g = want;
            else
                lim_g += (1.0f - lim_g) * rel;
            out_l[i] *= lim_g;
            out_r[i] *= lim_g;
        }
    }
}
/* SIDECHAIN (Ed, Nov 25: "the side chain option inside the Polish menu"): the tracks in SMASK (bit k, NTRK order; bit
 * NTRK LEAD; DRUMS, whose kick it follows, never) dip at every kick of DRUMS (core.h duck_kick: the kit's KICK lane, any
 * note of another engine there; heard or not) and come back over a part of the beat, so it follows the tempo. FEEL:
 * TIGHT (a 16th, straight back), CLASSIC (an 8th, curved), PUMP (a quarter: the whole beat breathes), SWELL (a quarter,
 * held down, then up). AMOUNT: how deep (127: -20 dB at the kick). Into the dip in ~2 ms (no click) */
static const struct { float beats, pow, hold; } SC_FEEL[4] = {{0.25f, 1.0f, 0.0f}, {0.5f, 2.0f, 0.0f}, {1.0f, 1.6f, 0.1f},
                                                             {1.0f, 0.7f, 0.45f}};
static float sc_g[NTRK + 1] = {1, 1, 1, 1, 1, 1}, sc_min = 1.0f;   /* each track's gain now; the deepest since the guide's look */
static void web_pol_side(uint32_t k, int32_t *b, int32_t *d, uint32_t n)
{
    uint32_t i, f;
    float len, depth, down = pol_ms(2.0f);
    if (!pol[PO_SON] || !((pol[PO_SMASK] >> k) & 1u) || !pol[PO_SAMT] || !duck_seen) {
        sc_g[k] = 1.0f;
        return;
    }
    f = (uint32_t)pol[PO_SFEEL] & 3u;
    len = SC_FEEL[f].beats * (float)beat_samples();
    depth = 1.0f - powf(10.0f, -20.0f * (float)pol[PO_SAMT] / 127.0f / 20.0f);   /* (the gain at the kick: -0 .. -20 dB) */
    for (i = 0; i < n; i++) {
        float x = (float)(uint32_t)(rondo_clock - duck_at + i) / len, dip, g;
        if (x >= 1.0f)
            dip = 0;
        else if (x < SC_FEEL[f].hold)
            dip = 1.0f;
        else
            dip = powf(1.0f - (x - SC_FEEL[f].hold) / (1.0f - SC_FEEL[f].hold), SC_FEEL[f].pow);
        g = 1.0f - depth * dip;
        sc_g[k] = g < sc_g[k] ? sc_g[k] + (g - sc_g[k]) * down : g;   /* (down in ~2 ms, up as the curve) */
        if (sc_g[k] < sc_min)
            sc_min = sc_g[k];
        b[i] = (int32_t)((float)b[i] * sc_g[k]);
        if (d)
            d[i] = (int32_t)((float)d[i] * sc_g[k]);
    }
}
/* the track's POLISH in the device: the compressor (on the whole track), then the sidechain */
static void web_pol_track(uint32_t k, int32_t *b, int32_t *d, uint32_t n)
{
    web_pol_comp(k, 0u, b, d, n);
    if (k)
        web_pol_side(k, b, d, n);
}
EXPORT void web_polish(uint32_t id, int32_t v)
{
    int32_t lo;
    if (id >= PO_N)
        return;
    lo = id == PO_ELO || id == PO_EMID || id == PO_EHI ? -12 : 0;
    pol[id] = v < lo ? lo : v > POL_MAX[id] ? POL_MAX[id] : v;
}
EXPORT int32_t web_polish_get(uint32_t id) { return id < PO_N ? pol[id] : 0; }
EXPORT uint32_t web_polish_part(void) { return pol_part_now(); }   /* (the part it's on now: tests) */
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

/* ------------------------------------------------------------------ the page's shortcuts (Ed, Nov 22: out of the menus)
 * a track row's tools (SEQ held's: ui_tools.c tl_do, with its undo copy): 0 BEAT (a drum track), 1 RANDOM, 2 VARY
 * (COOK), 3 CLEAR, on track k (selected first). 1 done (the screen says what), 0 not (it says why) */
EXPORT uint32_t web_tool(uint32_t k, uint32_t which)
{
    static const uint8_t TL[4] = {TL_BEAT, TL_RANDOM, TL_COOK, TL_CLEAR};
    const char *m;
    if (k >= NTRK || which > 3u)
        return 0;
    if (ph_focus && which == 3u) {                       /* LEAD focused: CLEAR its lane of this pattern (lead.c) */
        ui_message("LEAD CLEARED");
        (void)lead_clear();
        ui.force = 1;
        return 1;
    }
    lead_ulast = 0;
    bank_ulast = 0;
    if (k != song.sel)
        track_select(k);
    if (!tl_cell(&trk[k], TL[which])) {
        ui_message("BEAT: A DRUM TRACK'S");
        return 0;
    }
    if (!(m = tl_do(&trk[k], TL[which], 0)))
        return 0;
    ui_message(m);
    ui.force = 1;
    return 1;
}
/* UNDO (SAVE held's: a NEW / RANDOM just done, else the last load or tool, ui.c undo_swap; again: redo) */
EXPORT uint32_t web_undo(void)
{
    if (chain_busy()) {
        ui_message("STOP TO UNDO");
        return 0;
    }
    if (bank_undo()) {                                   /* a pattern copied / cleared / made the last thing done */
        ui.force = 1;
        return 1;
    }
    if (lead_undo()) {                                   /* LEAD's take or CLEAR the last thing done (lead.c) */
        ui_message("LEAD: UNDO");
        ui.force = 1;
        return 1;
    }
    if (!new_undo())
        undo_swap();
    ui.force = 1;
    return 1;
}
/* REC tapped while stopped (Ed, Nov 22: "a prompt ... if they want a metronome"): the page asked; c 1 with the
 * metronome (a bar counted in, then the clicks while it records), 0 without, 2 cancel (disarmed). The metronome as it
 * was comes back when the take is over (web_ms). REC armed while it already plays: no question, no metronome */
EXPORT void web_rec_go(uint32_t c)
{
    if (!web_rec_ask)
        return;
    web_rec_ask = 0;
    if (c == 2u) {
        song.rec = 0;
        ui.force = 1;
        return;
    }
    web_click_held = 1;
    web_rec_ran = 0;
    web_click_mode = (uint8_t)(c == 1u ? CLICK_ON : CLICK_OFF);
    web_cin_bars = (uint8_t)(c == 1u ? 1u : 0u);
    click_mode = web_click_mode;                          /* (now: the transport starts before the next frame's rp_apply) */
    cin_bars = web_cin_bars;
    transport_req = 1;
    ui.force = 1;
}
/* MIDI: 1 a keyboard plays the picked track (any channel), 0 the song's ROUT (POWER USER's MENU > MIDI) */
EXPORT void web_midi_sel_set(uint32_t on)
{
    web_midi_sel = (unsigned char)(on != 0);
    if (!on && web_route_was >= 0 && song.g[G_ROUTE] == ROUTE_SEL)
        song.g[G_ROUTE] = web_route_was;                 /* (by channel: the song's own ROUT again) */
    web_route_was = -1;
}
/* the tempo (40..240) and the swing (0..100), from the page's header */
EXPORT void web_tempo(uint32_t bpm) { song.g[G_BPM] = (int16_t)clamp((int32_t)bpm, GP[G_BPM].min, GP[G_BPM].max); ui.force = 1; }
EXPORT void web_swing(uint32_t v) { song.g[G_SWING] = (int16_t)clamp((int32_t)v, 0, 100); ui.force = 1; }
/* the key: ROOT (0..11), SCALE (params.c N_SCALE: 0 CHR = any note) and the keys kept in it (QNT SNAP) on the
 * melodic tracks (the kits' keys are their drums); the steps as they are */
EXPORT void web_key(uint32_t root, uint32_t scale, uint32_t snap)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        if (trk_is_kit(k))
            continue;
        trk[k].p[P_ROOT] = (int16_t)(root % 12u);
        trk[k].p[P_SCALE] = (int16_t)(scale % 16u);
        trk[k].p[P_QUANT] = (int16_t)(snap && scale ? 1 : 0);   /* (seq.c QN_SNAP: every key onto the scale) */
    }
    ui.force = 1;
}

EXPORT uint32_t web_rs_style(void) { return rsg.genre; }   /* the last RANDOM SONG's genre asked for (ST_*): tests */
EXPORT uint32_t web_rs_source(void) { return rsg.sty; }   /* .. and whose song it is (VAPORWAVE: another's) */
EXPORT void web_rs_salt(uint32_t x) { rs_salt = x; }   /* (the next RANDOM SONG's: the page's randomness, boot_song.c) */   /* the last RANDOM SONG's style (boot_song.c ST_*): tests */

/* NEW SONG (the page's button; the device's SAVE > TOOLS > NEW + DEFAULT SOUNDS): every track cleared, the power-on
 * sounds; RANDOM SONG (MENU > SYSTEM > RANDOM SONG): a song rolled. The page has asked first, so no arming here; playing
 * stops first. Either can be undone: SAVE held */
EXPORT void web_new_song(uint32_t random)          /* 0 NEW, 1 RANDOM (any genre), 2 + g RANDOM in genre g (boot_song.c ST_*:
                                                    * the pop-up's GENRE, Nov 26 2026) */
{
    if (chain.running || chain.armed)
        chain_stop();
    if (song.playing)
        seq_stop();
    if (random) {
        rs_arm_t = fm1_ms | 1u;                    /* (boot_song.c: confirmed) */
        rs_genre = (uint8_t)(random >= 2u && random - 2u < ST_N ? random - 2u : RS_ANY);
        random_song();
        rs_genre = RS_ANY;
    } else
        new_song(1);
    lead_clear_all();                              /* (LEAD's lanes: the old song's) */
    ui.force = 1;
}

/* the beginner's device: 1 the screen's words plain and the in-depth pages hidden (the page's default), 0 the device's
 * words and every page (POWER USER): gfx.c ui_plain, ui_simple */
/* FX's job (Nov 25: "the Glitch effects ... tied to the Jam Pads button and if you're in any other menu and press the FX
 * button you get all the original effects"): on 1 a tap locks the FX keys (JAM PADS, the box), and open 1 locks them now;
 * on 0 FX opens the effects pages, and the FX keys locked open are let go */
EXPORT void web_fx_mode(uint32_t on, uint32_t open)
{
    web_fx_keys = on ? 1u : 0u;
    if (on && open && ui.lock != LAYER_FX && !ui.menu && !name_on()) {
        lys.nv.home = ui.home; lys.nv.page = ui.page; lys.nv.act = ui.act; lys.nv.seq = song.seq_mode; lys.nv.rec = song.rec;
        memcpy(lys.nv.fam, ui.fam_last, sizeof lys.nv.fam);
        ui.ly = (uint8_t)LAYER_FX;
        layer_lock(LAYER_FX, fm1_ms);
    } else if (!on && ui.lock == LAYER_FX)
        ui.lock = 0;
    ui.force = 1;
}
EXPORT uint32_t web_fx_locked(void) { return ui.lock == LAYER_FX; }   /* (tests) */
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
    }    pol_master(n);                                     /* POLISH: EQ, EXCITE, the limiter (off: nothing) */
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
/* the buttons the screen's touch holds (ui_touch.c TOUCH_BTN), by label; a tap's release waits web_touch_rel_ms so the
 * UI sees it pressed for a frame or more */
static uint32_t web_touch_lab, web_touch_rel, web_touch_rel_ms;
static void web_btn_apply(void)
{
    uint32_t m = 0, b, old = fm1_in.buttons, labels = web_btn_labels | web_touch_lab;
    for (b = 0; b < NB; b++)
        if ((labels >> b) & 1u)
            m |= 1u << panel.btn[b];
    web_pressed |= m & ~old;
    web_released |= old & ~m;
    fm1_in.buttons = m;
}
EXPORT void web_buttons(uint32_t labels)
{
    web_btn_labels = labels;
    web_btn_apply();
}
static uint32_t web_touch_down_ms[NB];
static void web_touch_btn(uint32_t b, int on)
{
    if (b >= NB)
        return;
    if (on) {
        web_touch_lab |= 1u << b;
        web_touch_rel &= ~(1u << b);
        web_touch_down_ms[b] = fm1_ms;
        web_btn_apply();
    } else if (web_touch_lab >> b & 1u) {
        web_touch_rel |= 1u << b;                       /* (let go in web_touch_tick, a frame on at least) */
        if (web_touch_rel_ms < web_touch_down_ms[b] + 60u)
            web_touch_rel_ms = web_touch_down_ms[b] + 60u;
    }
}
static uint32_t web_touch_keys, web_touch_krel, web_touch_krel_ms, web_touch_kdown_ms;   /* (touch: a layer's cells) */
static void web_keys_apply(void);
static void web_touch_tick(void)
{
    if (web_touch_krel && (int32_t)(fm1_ms - web_touch_krel_ms) >= 0) {
        web_touch_keys &= ~web_touch_krel;
        web_touch_krel = 0;
        web_keys_apply();
    }
    if (web_touch_rel && (int32_t)(fm1_ms - web_touch_rel_ms) >= 0) {
        web_touch_lab &= ~web_touch_rel;
        web_touch_rel = 0;
        web_btn_apply();
    }
}
/* the screen touched (the page's pointer on the canvas): phase 0 down, 1 move, 2 up; x y in the screen's layout pixels */
EXPORT void web_touch(uint32_t phase, int32_t x, int32_t y) { ui_touch(phase, x, y); }
static void web_keys_apply(void)
{
    uint32_t old = fm1_in.notes, mask = (web_key_mask | web_touch_keys) & ((1u << 27) - 1u);
    web_notes_pressed |= mask & ~old;
    fm1_in.notes = mask;
}
EXPORT void web_keys(uint32_t mask)
{
    web_key_mask = mask & ((1u << 27) - 1u);
    web_keys_apply();
}
static void web_touch_key(uint32_t k, int on)
{
    if (k >= 27u)
        return;
    if (on) {
        web_touch_keys |= 1u << k;
        web_touch_krel &= ~(1u << k);
        web_touch_kdown_ms = fm1_ms;
        web_keys_apply();
    } else if (web_touch_keys >> k & 1u) {
        web_touch_krel |= 1u << k;                      /* (let go in web_touch_tick, a frame on at least) */
        web_touch_krel_ms = web_touch_kdown_ms + 60u;
    }
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
    if (web_midi_sel && (status & 0xF0u) != 0xB0u) {     /* a keyboard's notes (not its knobs' CCs) on the picked track:
                                                         * ROUT SEL while they arrive, the song's own ROUT back after */
        if (song.g[G_ROUTE] != ROUTE_SEL) {
            web_route_was = (int16_t)song.g[G_ROUTE];
            song.g[G_ROUTE] = ROUTE_SEL;
        }
    }
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

/* (tests / renders: a part's mix and envelope, -1000 leaves one as it is; the reverb bus's TYPE and SIZE) */
EXPORT void web_test_fx(uint32_t p, int32_t rev, int32_t chor, int32_t pan, int32_t det, int32_t atk, int32_t rel, int32_t dly)
{
    track_t *t = &trk[p % NPART];
    if (rev != -1000) t->p[P_REV] = (int16_t)rev;
    if (chor != -1000) t->p[P_CHOR] = (int16_t)chor;
    if (pan != -1000) t->p[P_PAN] = (int16_t)pan;
    if (det != -1000) t->p[P_DETUNE] = (int16_t)det;
    if (atk != -1000) t->p[P_ATK] = (int16_t)atk;
    if (rel != -1000) t->p[P_REL] = (int16_t)rel;
    if (dly != -1000) t->p[P_DLY] = (int16_t)dly;
}
EXPORT void web_test_reverb(uint32_t type, uint32_t size) { song.g[G_RTYPE] = (int16_t)type; song.g[G_RSIZE] = (int16_t)size; }
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
EXPORT uint32_t web_obj_lead(void) { return OBJ_LEAD; }          /* (LEAD's sound and lanes: lead.c; a song's id 7) */
/* LEAD focused from the page (1) or back to the track it came from (0): as TRACK past the last track does */
EXPORT void web_lead_focus(uint32_t on)
{
    if (on && !ph_focus) {
        if (!ui.home)
            go_home();
        ph_focus = 1;
        ph_page = 0;
    } else if (!on && ph_focus)
        ph_focus = 0;
    ui.force = 1;
}
EXPORT uint32_t web_lead_clear(void) { uint32_t p = lead_clear(); ui_message("LEAD CLEARED"); ui.force = 1; return p; }
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
    wj("\"recask\":%d,\"cin\":%d,\"click\":[%u,%u],", web_rec_ask, seq_counting() ? 1 : 0, (unsigned)click_mode, (unsigned)cin_bars);
    wj("\"swing\":%d,\"key\":[%d,%d,%d],", song.g[G_SWING], trk[1].p[P_ROOT], trk[1].p[P_SCALE], trk[1].p[P_QUANT]);
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
        wj_str(plain_text(ENGINES[t->eng_req % NENGINES]->name));   /* (plain: the drum engine is KIT) */
        wj(",\"pan\":%d", t->p[P_PAN]);
        wj(",\"lv\":%d,\"mute\":%d,\"solo\":%d,\"b\":%d,\"step\":%u,\"len\":%d,\"vox\":%u,\"pats\":[", t->p[P_LEVEL], t->p[P_MUTE] ? 1 : 0,
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
        wj_str(plain_text(full));                           /* (plain: 120 BPM, not HBT) */
        wj(",\"frac\":%d}", on && web_card_now[k].ratio >= 0 ? web_card_now[k].ratio : -1);
    }
    wj("],\"xy\":[%u,%u,%u,%u,%u,%u],\"xyk\":%d", xy_live[0].x, xy_live[0].y, xy_live[0].on, xy_live[1].x, xy_live[1].y, xy_live[1].on,
       web_pad ? (int)web_pad_k : -1);
    wj(",\"vapor\":[%u,%u,%u,%u]", vap_get(0), vap_get(1), vap_get(2), vap_get(3));
    wj(",\"pal\":%u", (unsigned)palette_cur());
    wj(",\"polgr\":%d", (int)(pc_grmin * -10.0f));   /* POLISH: the compressor's deepest since the last (dB x 10) */
    wj(",\"polsc\":%d", (int)(-200.0f * log10f(sc_min > 1e-4f ? sc_min : 1e-4f)));   /* (the sidechain's, dB x 10) */
    sc_min = 1.0f;
    wj(",\"polk\":[%d,\"%s\",\"%s\"]", ENG_KIT(trk[0].engine % NENGINES) ? 1 : 0,   /* (POLISH's parts: DRUMS a kit?, */
       N_PC_SND[pc_snd(trk[PERC_TRK].p, 0)], N_PC_SND[pc_snd(trk[PERC_TRK].p, 1)]);       /* PERC's two sounds) */
    pc_grmin = 0;
    {   /* LEAD (lead.c): focused, its sound, its engine, recording, the lane playing, the notes a pattern */
        uint32_t p;
        wj(",\"lead\":{\"f\":%d,\"rec\":%d,\"pat\":%u,\"lv\":%d,\"pan\":%d,\"mute\":%d,\"solo\":%d,\"snd\":", ph_focus ? 1 : 0,
           lead_recording() ? 1 : 0, (unsigned)lead_pat(), ph_trk.p[P_LEVEL], ph_trk.p[P_PAN], ph_trk.p[P_MUTE] ? 1 : 0, (web_solo >> NTRK) & 1u);
        wj_str(ph_voice_name());
        wj(",\"eng\":");
        wj_str(plain_text(ENGINES[ph_trk.eng_req % NENGINES]->name));
        wj(",\"n\":[");
        for (p = 0; p < LEAD_PATS; p++)
            wj(p ? ",%u" : "%u", (unsigned)lead_count(p));
        wj("]}");
    }
    web_buttons_json();
    wj("}");
    web_js[web_jn] = 0;
    return web_js;
}
EXPORT uint32_t web_guide_len(void) { return web_jn; }
#ifdef TEXT_LOG                                          /* (the debug build's word lint: gfx.c text_log) */
EXPORT const char *web_text_log(void) { text_log[text_log_n] = 0; return text_log; }
EXPORT uint32_t web_text_log_n(void) { return text_log_n; }
#endif
/* the page's controls */
EXPORT void web_track(uint32_t k)
{
    if (k < NTRK && ph_focus) {                          /* (from LEAD: back to the tracks) */
        ph_focus = 0;
        ui.force = 1;
    }
    if (k < NTRK && k != song.sel) { track_select(k); ui.force = 1; }
}
EXPORT void web_mute(uint32_t k)
{
    if (k < NTRK)
        trk[k].p[P_MUTE] = (int16_t)!trk[k].p[P_MUTE];
    else if (k == NTRK) {                                   /* LEAD (phantom.c: silent while muted; kept: lead.c) */
        ph_trk.p[P_MUTE] = (int16_t)!ph_trk.p[P_MUTE];
        lead_dirty = 1;
    }
    ui.force = 1;
}
/* PAN from the page's mixer (Nov 25): -64 .. 63, k NTRK = LEAD */
EXPORT void web_pan(uint32_t k, int32_t v)
{
    int16_t p = (int16_t)clamp(v, -64, 63);
    if (k < NTRK)
        trk[k].p[P_PAN] = p;
    else if (k == NTRK) {
        ph_trk.p[P_PAN] = p;
        lead_dirty = 1;
    }
    ui.force = 1;
}
/* the page's mixer (Control Room, Nov 24): a track's LEVEL (P_LEVEL 0..127, as MIX's faders), k = NTRK: LEAD's */
EXPORT void web_level(uint32_t k, uint32_t v)
{
    if (v > 127u) v = 127u;
    if (k < NTRK) trk[k].p[P_LEVEL] = (int16_t)v;
    else { ph_trk.p[P_LEVEL] = (int16_t)v; lead_dirty = 1; }
    ui.force = 1;
}
/* the screen's palette from the page (its THEME / SCREEN pickers, Nov 25): MENU > COLOR's built-in i (gen_ui_palettes.py's
 * order), saved with the settings like a pick in the menu */
EXPORT void web_palette(uint32_t i) { if (i < NPALETTES && (cpal_on() || settings.palette != i)) palette_choose(i); }
EXPORT void web_solo_toggle(uint32_t k) { if (k <= NTRK) { web_solo ^= (unsigned char)(1u << k); ui.force = 1; } }   /* (k NTRK: LEAD) */
EXPORT uint32_t web_cue(uint32_t p) { uint32_t rc = p < NBANK ? (uint32_t)bank_key(p) : 1u; ui.force = 1; return rc; }
/* the FX PAD under the mouse: crosshair j at x y (0..127), on; on 0: let go (it springs back), and the pad closes
 * once both are let go */
/* the page's FX PAD j on track k (both pads are on one track: a new track takes them, the page sends the other's
 * again); held or LOCKED (index.html), it stays on k whichever track is picked after */
EXPORT void web_xy(uint32_t j, uint32_t x, uint32_t y, uint32_t on, uint32_t k)
{
    if (j > 1u)
        return;
    web_pad = 1;
    web_pad_k = (unsigned char)(k < NTRK ? k : song.sel % NTRK);
    xy_open(web_pad_k);
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
    if (b == B_EDIT && ui.home && ph_focus && !ui.menu) {   /* LEAD (PHANTOM): EDIT flips its knobs' two pages */
        str_cpy(act, ph_page ? "KNOBS 1" : "KNOBS 2", 24);
        return;
    }
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
    if (b == B_FX && (web_fx_keys || ui.lock == LAYER_FX)) {   /* (ui_layer.c WEB_FX_LOCK: a tap locks the FX keys) */
        str_cpy(act, ui.lock == LAYER_FX ? "FX KEYS OFF" : "FX KEYS", 24);
        return;
    }
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
