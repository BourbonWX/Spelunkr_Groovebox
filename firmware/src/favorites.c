/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define FELUCCA_FAVORITES 1
/* Stable engine/preset references; fixed capacity independent of optional engines. */
typedef struct {
    uint8_t factory[16][32]; /* engine 0..15, preset 0..255 */
    uint32_t user, filter;
} favorites_t;
static favorites_t favorites __attribute__((section(".pool")));
/* the row of engine e's favourites: its number below 14 (rows 14, 15 are MIDI LEARN's and the settings'); Phantom
 * Cosmonaut's engines, all 14 and up, in the rows of engines retired since Dungeon Tracker 1.1 (no favourite of
 * theirs could be set since): MORPH 2, FM 6, the NOISE kit 7, DEBRIS 10 (FOLDER's 3 and GRANULAR's 5 left as they
 * were: retired, -> DEBRIS; 11 was 0.9.4's NUMBERS'). Phantom Tracker: ANALOG its own 0, GENESIS in DIGITAL's 1 (never
 * offered), SID in SLICE's 13 (retired in Dungeon Tracker 1.0). -1 = none */
static int32_t fav_row(uint32_t e)
{
    switch (e) {
    case ENGI_MORPH: return 2;
    case ENGI_DEBRIS: return 10;
    case ENGI_GENESIS: return 1;
    case ENGI_SID: return 13;
    case ENGI_PFM: return 6;
    case ENGI_CKIT: return 7;
    case ENGI_GLORP: return 11;          /* (0.9.3 VOX's row) */
    case ENGI_VULT: return 9;            /* (PHYS's, retired) */
    default: return e < 14u ? (int32_t)e : -1;
    }
}
static int favorite_has(uint32_t engine, uint32_t preset)
{
    int32_t r = fav_row(engine);
    if (engine == NENGINES)
        return preset < UP_SLOTS && ((favorites.user >> preset) & 1u);
    return engine < NENGINES && r >= 0 && preset < 256u &&
        ((favorites.factory[r][preset / 8u] >> (preset % 8u)) & 1u);
}
static int favorite_set(uint32_t engine, uint32_t preset, int on)
{
    if (engine > NENGINES || (engine == NENGINES ? preset >= UP_SLOTS : fav_row(engine) < 0 || preset >= 256u))
        return 0;
    if (favorite_has(engine, preset) == !!on) return 0;
    if (engine == NENGINES) {
        if (on) favorites.user |= 1u << preset;
        else favorites.user &= ~(1u << preset);
    } else {
        uint8_t *b = &favorites.factory[fav_row(engine)][preset / 8u];
        if (on) *b |= (uint8_t)(1u << (preset % 8u));
        else *b &= (uint8_t)~(1u << (preset % 8u));
    }
    return 1;
}

/* MENU > DISPLAY > COLOR's 11th value, CUSTOM (1.6, #244, Discussion #239): a palette of the user's, five RGB565 colours (BG
 * SURF TEXT THEME ACCENT, 16 bits little endian) in favorites.factory[15][0..9], flags in [12] (bit 0: one is stored, bit 1:
 * it is the palette in use), set from the editor (PALETTE_SET, editor_palette.c) and saved with the settings. settings.palette
 * stays a built-in palette (the one in use before, or after): firmware before 1.6 shows that and keeps the bytes. The value
 * is in COLOR only while one is stored. Zero (every older setting) = none */
#define ui_cpal (&favorites.factory[15][0])
#define ui_cpal_flags (favorites.factory[15][12])
#define CPAL_STORED 1u
#define CPAL_ACTIVE 2u
static uint32_t cpal_on(void) { return (ui_cpal_flags & (CPAL_STORED | CPAL_ACTIVE)) == (CPAL_STORED | CPAL_ACTIVE); }
static uint32_t palette_n(void) { return NPALETTES + ((ui_cpal_flags & CPAL_STORED) ? 1u : 0u); }     /* COLOR's values */
static uint32_t palette_cur(void) { return cpal_on() ? NPALETTES : settings.palette % NPALETTES; }
static const char *palette_name(uint32_t v) { return v == NPALETTES ? "CUSTOM" : UI_PALETTES[v % NPALETTES].name; }
/* the palette in use into gfx.c (the settings changed under it: a start, an import, a restore, a STYLE change) */
static void palette_apply(void)
{
    if (cpal_on()) {
        palette_custom_load(ui_cpal);
        palette_set(NPALETTES);
    } else {
        palette_set(settings.palette % NPALETTES);
    }
}

/* the five colours into the settings' bytes (stored; not yet the palette in use: palette_choose / palette_apply) */
static void cpal_store(const uint16_t *c)
{
    uint32_t i;
    for (i = 0; i < 5u; i++) {
        ui_cpal[2u * i] = (uint8_t)c[i];
        ui_cpal[2u * i + 1u] = (uint8_t)(c[i] >> 8);
    }
    ui_cpal_flags |= CPAL_STORED;
}
