/*
 * The EmuUtility application.
 *
 * Copyright (C) 2026 Chris Turner <chris_purusha@icloud.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifdef __cplusplus
extern "C" {
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define GL_SILENCE_DEPRECATION    1
#include <GLFW/glfw3.h>
#pragma clang diagnostic pop

#include <math.h>

#include "defs.h"
#include "synthlibDefs.h"
#include "types.h"
#include "globalVars.h"
#include "utilsGraphics.h"
#include "emuGraphics.h"
#include "peptalk.h"
#include "mouseHandle.h"
#include "clickRegion.h"

static GLuint   gLcdTexture  = 0;
static uint32_t gLastRefresh = 0xFFFFFFFF;

// ── Dial (large knob, right of LCD) ──────────────────────────────────────────

#define DIAL_CX        1100.0
#define DIAL_CY        (74.0 + MENU_BAR_HEIGHT)   // vertically centred on the LCD
#define DIAL_RADIUS    60.0
#define DIAL_RANGE     128

static uint32_t gDialValue   = 0;

// ── LCD texture ───────────────────────────────────────────────────────────────

void init_lcd_texture(void) {
    // TODO - move into utilsGraphics

    glGenTextures(1, &gLcdTexture);
    glBindTexture(GL_TEXTURE_2D, gLcdTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, LCD_WIDTH, LCD_HEIGHT, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void update_lcd_texture(void) {
    // Expand 1-bit pixels to RGBA for the texture
    static uint8_t rgba[LCD_WIDTH * LCD_HEIGHT * 4];
    const tRgb     fg = (tRgb)RGB_LCD_FG;
    const tRgb     bg = (tRgb)RGB_LCD_BG;

    for (int byte = 0; byte < LCD_BYTES; byte++) {
        uint8_t b = gLcd.pixels[byte];

        for (int bit = 7; bit >= 0; bit--) {
            int          pixelIdx = (byte * 8) + (7 - bit);
            int          rgbaIdx  = pixelIdx * 4;
            bool         lit      = (b >> bit) & 1;
            const tRgb * col      = lit ? &fg : &bg;

            rgba[rgbaIdx + 0] = (uint8_t)(col->red * 255.0);
            rgba[rgbaIdx + 1] = (uint8_t)(col->green * 255.0);
            rgba[rgbaIdx + 2] = (uint8_t)(col->blue * 255.0);
            rgba[rgbaIdx + 3] = 255;
        }
    }

    // TODO - move into utilsGraphics, possibly with the above too
    glBindTexture(GL_TEXTURE_2D, gLcdTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LCD_WIDTH, LCD_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    gLastRefresh = gLcd.refresh;
}

void render_lcd() {
    if (gLcdTexture == 0) {
        return;
    }

    if (gLcd.refresh != gLastRefresh) {
        update_lcd_texture();
    }
    double x = 200;
    double y = 20 + MENU_BAR_HEIGHT;
    double w = 500;
    double h = 120;

    // Green border around the display area
    set_rgb_colour((tRgb)RGB_LCD_BG);
    render_rectangle(mainArea, (tRectangle){{x - LCD_BORDER, y - LCD_BORDER},
                                            {w + LCD_BORDER * 2.0, h + LCD_BORDER * 2.0}});

    if (!gSessionOpen) {
        return;
    }
    render_rectangle(mainArea, (tRectangle){{x - LCD_BORDER, y - LCD_BORDER},
                                            {w + LCD_BORDER * 2.0, h + LCD_BORDER * 2.0}});

    render_texture(mainArea, (tRectangle){{x, y},
                                          {w, h}}, gLcdTexture);
}

void render_dial_knob(void) {
    tCoord centre = {DIAL_CX, DIAL_CY};

    // Full-circle disc
    set_rgb_colour((tRgb){0.30, 0.30, 0.30});
    render_circle_part_angle(mainArea, centre, DIAL_RADIUS, 0.0, 360.0, 36);

    // Outer ring
    set_rgb_colour((tRgb){0.55, 0.55, 0.55});
    render_circle_line(mainArea, centre, DIAL_RADIUS, 36, 4.0);

    // Indicator line — full 360° rotation, starting from 12 o'clock
    double angle  = 270.0 + ((double)gDialValue / (double)DIAL_RANGE) * 360.0;

    set_rgb_colour((tRgb){0.90, 0.90, 0.90});
    render_radial_line(mainArea, centre, DIAL_RADIUS * 0.75, angle, 5.0);

    register_click_region(emu_dial_rect(), eClickLayerPanel, dial_press_click_handler, NULL);
}

tRectangle emu_dial_rect(void) {
    return (tRectangle){{
                            DIAL_CX - DIAL_RADIUS, DIAL_CY - DIAL_RADIUS
                        }, {DIAL_RADIUS * 2.0, DIAL_RADIUS * 2.0}};
}

bool dial_hit_test(tCoord coord) {
    return within_rectangle(coord, emu_dial_rect());
}

void dial_nudge(int delta) {
    int next = ((int)gDialValue + delta % (int)DIAL_RANGE + (int)DIAL_RANGE * 64) % (int)DIAL_RANGE;

    gDialValue = (uint32_t)next;

    if (gSessionOpen) {
        peptalk_send_rotary_event(delta);
        // No LCD request here — while a drag is active, the MIDI poll
        // thread already polls for a delta on its own throttled cadence
        // (see gDialDragActive), independent of individual ticks.
    }
    gReDraw    = true;
}

static double gDialAngleAccum = 0.0; // fractional degrees of rotation not yet turned into a step

void dial_nudge_by_angle(double deltaDegrees) {
    // Rotate at the same angular rate as the mouse (1° of mouse rotation ==
    // 1° of visual dial rotation), without pinning the indicator to the raw
    // mouse angle — matches how a real endless encoder is driven, rather than
    // a bounded pot that snaps to wherever you click.
    double degreesPerStep = 360.0 / (double)DIAL_RANGE;

    gDialAngleAccum += deltaDegrees;

    int    steps          = (int)(gDialAngleAccum / degreesPerStep);

    if (steps != 0) {
        gDialAngleAccum -= (double)steps * degreesPerStep;
        dial_nudge(steps);
    }
}

// ── Button layout ─────────────────────────────────────────────────────────────
// Matches the physical E4/E5000 front panel layout.

// Left-panel button geometry
#define LP_W          84.0
#define LP_H          20.0
#define LP_GAP        4.0
#define LP_ROW        (LP_H + LP_GAP)

// Right-section absolute x positions
#define RP_DEC_X      896.0  // DEC button
#define RP_DEC_W      60.0
#define RP_NAV_X      920.0  // navigation cluster left edge
#define RP_NAV_SZ     20.0   // nav arrow button size (square)
#define RP_NAV_STR    24.0   // nav stride (NAV_SZ + LP_GAP)
#define RP_NP_X       1032.0 // numpad left edge
#define RP_NP_W       56.0   // numpad button width
#define RP_NP_STR     60.0   // numpad stride (NP_W + LP_GAP)
#define RP_INC_X      1216.0 // INC button
#define RP_INC_W      60.0

static tButton   gButtons[] = {
    // Row 0 — main section buttons
    {pkPresetManage,    {0}, "Preset",   false, false, 0},
    {pkSampleManage,    {0}, "Sample",   false, false, 0},
    {pkPresetEdit,      {0}, "Prs Edit", false, false, 0},
    {pkSampleEdit,      {0}, "Smp Edit", false, false, 0},
    {pkMaster,          {0}, "Master",   false, false, 0},
    {pkDisk,            {0}, "Disk",     false, false, 0},
    // Row 0 continued — sequence controls
    {pkSequencer,       {0}, "Seqcr",    false, false, 0},
    {pkSeqRtz,          {0}, "RTZ",      false, false, 0},
    {pkSeqRew,          {0}, "Rew",      false, false, 0},
    {pkSeqFfwd,         {0}, "FFwd",     false, false, 0},
    {pkSeqStop,         {0}, "Stop",     false, false, 0},
    {pkSeqPlay,         {0}, "Play",     false, false, 0},
    {pkSeqRec,          {0}, "Rec",      false, false, 0},

    // Row 1 — assign / function keys
    {pkAssign1,         {0}, "Assign 1", false, false, 0},
    {pkAssign2,         {0}, "Assign 2", false, false, 0},
    {pkAssign3,         {0}, "Assign 3", false, false, 0},
    {pkF1,              {0}, "F1",       false, false, 0},
    {pkF2,              {0}, "F2",       false, false, 0},
    {pkF3,              {0}, "F3",       false, false, 0},
    {pkF4,              {0}, "F4",       false, false, 0},
    {pkF5,              {0}, "F5",       false, false, 0},
    {pkF6,              {0}, "F6",       false, false, 0},

    // Row 2 — navigation and numeric
    {pkPrev,            {0}, "Prev",     false, false, 0},
    {pkNext,            {0}, "Next",     false, false, 0},
    {pkUp,              {0}, "^",        false, false, 0},
    {pkDown,            {0}, "v",        false, false, 0},
    {pkLeft,            {0}, "<",        false, false, 0},
    {pkRight,           {0}, ">",        false, false, 0},
    {pkEnter,           {0}, "Enter",    false, false, 0},
    {pkExit,            {0}, "Exit",     false, false, 0},
    {pkDec,             {0}, "Dec",      false, false, 0},
    {pkInc,             {0}, "Inc",      false, false, 0},
    {pkAudition,        {0}, "Audition", false, false, 0},
    {pkControlsFx,      {0}, "Ctrl/FX",  false, false, 0},

    // Row 3 — numeric pad
    {pkNumpad1,         {0}, "1",        false, false, 0},
    {pkNumpad2,         {0}, "2",        false, false, 0},
    {pkNumpad3,         {0}, "3",        false, false, 0},
    {pkNumpad4,         {0}, "4",        false, false, 0},
    {pkNumpad5,         {0}, "5",        false, false, 0},
    {pkNumpad6,         {0}, "6",        false, false, 0},
    {pkNumpad7,         {0}, "7",        false, false, 0},
    {pkNumpad8,         {0}, "8",        false, false, 0},
    {pkNumpad9,         {0}, "9",        false, false, 0},
    {pkNumpad0,         {0}, "0",        false, false, 0},
    {pkNumpadDot,       {0}, ".",        false, false, 0},
    {pkNumpadPlusMinus, {0}, "+/-",      false, false, 0}, };

static const int NUM_BUTTONS = (int)(sizeof(gButtons) / sizeof(gButtons[0]));

double button_panel_height(double areaWidth) {
    (void)areaWidth;
    // 1 DEC/INC row + 4 numpad rows, bordered by LP_GAP margins
    return 5.0 * LP_H + 6.0 * LP_GAP;
}

static tButton * find_button(tButtonKey key) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (gButtons[i].key == key) {
            return &gButtons[i];
        }
    }

    return NULL;
}

// Buttons act on both press and release (unlike the dial, which only arms on
// press) — mirrors the "btn->pressed = pressed" line previously in
// handle_mouse_button()'s button_at() branch.
static void button_click_handler(tCoord coord, eClickPhase phase, void * userData) {
    (void)coord;
    tButton * btn     = (tButton *)userData;
    bool      pressed = (phase == eClickPress);

    LOG_DEBUG("hit button key=%d label=%s\n", (int)btn->key, btn->label);
    btn->pressed  = pressed;
    peptalk_send_button_event(btn->key, pressed);
    // Always request a full dump after any button press. Deltas are only
    // safe when we know the hardware's base state hasn't drifted — button
    // presses can change the display in ways that compound delta errors.
    gNeedLcdFull  = true;
    gNeedLcdDelta = false;
    gReDraw       = true;
}

static void render_button_at(tButtonKey key, double x, double y, double w, double h) {
    tButton * btn   = find_button(key);

    if (btn == NULL) {
        return;
    }
    btn->rectangle = (tRectangle){{
                                      x, y
                                  }, {w, h}};
    register_click_region(btn->rectangle, eClickLayerPanel, button_click_handler, btn);

    uint32_t  leds  = gLeds;
    bool      ledOn = btn->hasLed && (leds & (1u << btn->ledIndex));
    tRgb      col   = btn->pressed ? (tRgb)RGB_AMBER : ledOn ? (tRgb)RGB_GREEN_ON : (tRgb)RGB_GREY_3;

    set_rgb_colour(col);
    render_rectangle(mainArea, btn->rectangle);
    set_rgb_colour((tRgb)RGB_WHITE);
    render_text(mainArea, (tRectangle){{x + 4.0, y + h * 0.2}, {0.0, h * 0.6}}, (char *)btn->label);
}

void render_button_panel() {
    double                  ox          = 10;
    double                  oy          = 160 + MENU_BAR_HEIGHT;

    // Clear stale hit rectangles for buttons not placed in this layout
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gButtons[i].rectangle = (tRectangle){{
                                                 0.0, 0.0
                                             }, {0.0, 0.0}};
    }

    // Row y positions
    double                  r0          = oy + LP_GAP;
    double                  r1          = r0 + LP_ROW;
    double                  r2          = r1 + LP_ROW;

    // ── Left panel: 2 rows × 10 columns ──────────────────────────────────────
    static const tButtonKey lp_row0[10] = {pkMaster, pkPresetManage, pkPresetEdit, pkAudition,
                                           pkF1,     pkF2,           pkF3,         pkF4,      pkF5, pkF6};
    static const tButtonKey lp_row1[10] = {pkDisk,    pkSampleManage, pkSampleEdit,
                                           pkAssign1, pkAssign2,      pkAssign3,
                                           pkExit,    pkPrev,         pkNext, pkEnter};

    for (int c = 0; c < 10; c++) {
        double x = ox + LP_GAP + c * (LP_W + LP_GAP);
        render_button_at(lp_row0[c], x, r0, LP_W, LP_H);
        render_button_at(lp_row1[c], x, r1, LP_W, LP_H);
    }

    // ── Right section ─────────────────────────────────────────────────────────
    // DEC and INC flank the nav/numpad cluster at the top row
    render_button_at(pkDec, ox + RP_DEC_X, r0, RP_DEC_W, LP_H);
    render_button_at(pkInc, ox + RP_INC_X, r0, RP_INC_W, LP_H);

    // Navigation: Up centred above Left / Down / Right (shifted down one row)
    render_button_at(pkUp, ox + RP_NAV_X + RP_NAV_STR, r2, RP_NAV_SZ, LP_H);
    render_button_at(pkLeft, ox + RP_NAV_X, r2 + LP_ROW, RP_NAV_SZ, LP_H);
    render_button_at(pkDown, ox + RP_NAV_X + RP_NAV_STR, r2 + LP_ROW, RP_NAV_SZ, LP_H);
    render_button_at(pkRight, ox + RP_NAV_X + 2.0 * RP_NAV_STR, r2 + LP_ROW, RP_NAV_SZ, LP_H);

    // Numpad: 3 columns × 4 rows
    static const tButtonKey np[4][3] = {
        {pkNumpad1,         pkNumpad2, pkNumpad3  },
        {pkNumpad4,         pkNumpad5, pkNumpad6  },
        {pkNumpad7,         pkNumpad8, pkNumpad9  },
        {pkNumpadPlusMinus, pkNumpad0, pkNumpadDot}, };

    for (int nr = 0; nr < 4; nr++) {
        double ny = r1 + nr * LP_ROW;

        for (int nc = 0; nc < 3; nc++) {
            render_button_at(np[nr][nc], ox + RP_NP_X + nc * RP_NP_STR, ny, RP_NP_W, LP_H);
        }
    }
}

tButton * button_at(tCoord coord) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (within_rectangle(coord, gButtons[i].rectangle)) {
            return &gButtons[i];
        }
    }

    return NULL;
}

#ifdef __cplusplus
}
#endif
