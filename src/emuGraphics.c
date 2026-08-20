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
#include "midiComms.h"
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

// ── Front-panel button layout ─────────────────────────────────────────────────
// Matches the physical E4/E5000 front panel layout. Declared up here, ahead of the LCD, because
// the LCD's own position and size are derived FROM it — see the soft-key block below.

// Left-panel button geometry
#define LP_W           84.0
#define LP_H           20.0
#define LP_GAP         4.0
#define LP_ROW         (LP_H + LP_GAP)

#define LP_ORIGIN_X    10.0
#define LP_ORIGIN_Y    (160.0 + MENU_BAR_HEIGHT)
#define LP_COL_X(c)    (LP_ORIGIN_X + LP_GAP + (c) * (LP_W + LP_GAP))
#define LP_FKEY_COL    4           // F1 is the fifth button along the top row; F2..F6 follow it

// ── LCD soft keys ─────────────────────────────────────────────────────────────
// The six boxes the sampler draws along the bottom of its own display are its soft keys, and F1..F6
// are the buttons that press them. These are their positions WITHIN the 240x64 bitmap, in device
// pixels, read straight off a live E5000 (2026-08-19) with the LCDDUMP backdoor command in
// graphics.c, which prints the raw bitmap as an ASCII grid for exactly this purpose.
//
// The device divides the full 240-pixel width into six exact 40-pixel cells at x = 0, 40, 80, 120,
// 160, 200, draws a 39-pixel rounded box in each (one pixel of gutter between neighbours), and the
// row occupies y = 51..63 — flush with the bottom edge of the display. The click target is the
// whole 40-pixel cell rather than the 39-pixel box, so the gutters aren't dead pixels between two
// live keys.
//
// EMU_SOFTKEY_COUNT itself lives in emuGraphics.h — graphics.c's backdoor walks the boxes too.
#define LCD_SOFTKEY_X        0.0    // left edge of the first cell
#define LCD_SOFTKEY_PITCH    40.0   // cell-to-cell stride
#define LCD_SOFTKEY_W        40.0   // full cell width (the drawn box inside it is 39)
#define LCD_SOFTKEY_Y        51.0   // top edge of the box row

// How far down the display the click-to-Exit zone reaches, in device pixels.
//
// Deliberately NOT "everything above the soft keys". The sampler stacks things there: Utils raises a
// second row of boxes, and a popup such as Sample Info puts an OK button well above the normal band.
// Treating that whole area as Exit would fire Exit at a button the user was aiming for. The top half
// is title and value text on every screen seen so far, and rows 32-50 are left as a dead margin
// rather than assumed safe.
#define LCD_EXIT_ZONE_H    32.0
#define LCD_SOFTKEY_H      13.0     // through to the last row of the display

// The LCD is placed and sized FROM the F-key geometry rather than independently, so each soft-key
// box lands directly above the button that presses it and stays there if either the button grid or
// the measured box geometry is ever adjusted. Scaling the bitmap so one box spans exactly one
// button pitch fixes the width; centring box 0 on F1 then fixes the left edge.
#define LCD_SCALE_X    ((LP_W + LP_GAP) / LCD_SOFTKEY_PITCH)
#define LCD_W          (LCD_WIDTH * LCD_SCALE_X)
#define LCD_H          120.0
#define LCD_SCALE_Y    (LCD_H / (double)LCD_HEIGHT)
#define LCD_X          (LP_COL_X(LP_FKEY_COL) + (LP_W * 0.5) - LCD_SCALE_X * (LCD_SOFTKEY_X + (LCD_SOFTKEY_W * 0.5)))
#define LCD_Y          (20.0 + MENU_BAR_HEIGHT)

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

    // Snapshot the shared LCD buffer under gLcdMutex before expanding it. The
    // CoreMIDI callback thread mutates gLcd.pixels in place (full memcpy or an
    // in-place XOR delta), so reading it directly across the whole expansion
    // loop could tear a frame. Copy out fast, then expand + upload the local
    // copy with the lock released (no reason to hold it over the GL call).
    uint8_t        pixels[LCD_BYTES];
    uint32_t       snapshotRefresh;

    pthread_mutex_lock(&gLcdMutex);
    memcpy(pixels, gLcd.pixels, LCD_BYTES);
    snapshotRefresh = gLcd.refresh;
    pthread_mutex_unlock(&gLcdMutex);

    for (int byte = 0; byte < LCD_BYTES; byte++) {
        uint8_t b = pixels[byte];

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
    gLastRefresh = snapshotRefresh;   // matches the pixels we just uploaded, not a possibly-newer value
}

tRectangle emu_lcd_rect(void) {
    return (tRectangle){{
                            LCD_X, LCD_Y
                        }, {
                            LCD_W, LCD_H
                        }
    };
}

tRectangle emu_softkey_rect(int index) {
    tRectangle rect = {{0.0, 0.0}, {0.0, 0.0}};

    if ((index < 0) || (index >= EMU_SOFTKEY_COUNT)) {
        return rect;
    }
    rect.coord.x = LCD_X + LCD_SCALE_X * (LCD_SOFTKEY_X + ((double)index * LCD_SOFTKEY_PITCH));
    rect.coord.y = LCD_Y + LCD_SCALE_Y * LCD_SOFTKEY_Y;
    rect.size.w  = LCD_SCALE_X * LCD_SOFTKEY_W;
    rect.size.h  = LCD_SCALE_Y * LCD_SOFTKEY_H;
    return rect;
}

tRectangle emu_lcd_body_rect(void) {
    // The TOP band only — see LCD_EXIT_ZONE_H for why this stops well short of the soft keys rather
    // than running down to them.
    return (tRectangle){{
                            LCD_X, LCD_Y
                        }, {
                            LCD_W, LCD_SCALE_Y * LCD_EXIT_ZONE_H
                        }
    };
}

// Clicking the body of the display — the area with no soft key under it — issues Exit. That mirrors
// what the hardware's own Exit key does: back out of wherever you are. The soft-key boxes keep their
// own regions, so only the part of the screen with nothing under it behaves this way.
static void lcd_body_click_handler(tCoord coord, eClickPhase phase, void * userData) {
    (void)coord;
    (void)userData;
    emu_button_press(pkExit, phase == eClickPress);
}

// F1..F6 are not contiguous in the PEPTALK key numbering (they interleave with Assign 3, Audition,
// Ctrl/FX, Prev and Next — see tButtonKey), so the mapping is spelled out rather than computed.
static const tButtonKey gSoftKeyOrder[EMU_SOFTKEY_COUNT] = {pkF1, pkF2, pkF3, pkF4, pkF5, pkF6};

tButtonKey emu_softkey_button(int index) {
    return ((index < 0) || (index >= EMU_SOFTKEY_COUNT)) ? (tButtonKey)0 : gSoftKeyOrder[index];
}

// Clicking a box on the display is exactly the same event as clicking the F-key beneath it, so it
// goes through the same handler — including that handler's press/release semantics and its
// "always ask for a full LCD dump afterwards" rule. userData is the index, not a pointer, because
// the boxes are not objects: they are regions computed from the layout each frame.
static void softkey_click_handler(tCoord coord, eClickPhase phase, void * userData) {
    (void)coord;
    emu_button_press(emu_softkey_button((int)(intptr_t)userData), phase == eClickPress);
}

void render_lcd() {
    if (gLcdTexture == 0) {
        return;
    }

    if (gLcd.refresh != gLastRefresh) {
        update_lcd_texture();
    }
    tRectangle lcd = emu_lcd_rect();

    // Green border around the display area
    set_rgb_colour((tRgb)RGB_LCD_BG);
    render_rectangle(mainArea, (tRectangle){{lcd.coord.x - LCD_BORDER, lcd.coord.y - LCD_BORDER},
                                            {lcd.size.w + LCD_BORDER * 2.0, lcd.size.h + LCD_BORDER * 2.0}
                     });

    if (!gSessionOpen) {
        return;
    }
    render_texture(mainArea, lcd, gLcdTexture);

    // The soft-key boxes are only live while a session is open — with no display content there is
    // nothing on them to press.
    // Registered BEFORE the soft keys so those sit on top of it — the body is the fallback for the
    // part of the display with nothing under it.
    register_click_region(emu_lcd_body_rect(), eClickLayerPanel, lcd_body_click_handler, NULL);

    for (int i = 0; i < EMU_SOFTKEY_COUNT; i++) {
        register_click_region(emu_softkey_rect(i), eClickLayerPanel, softkey_click_handler, (void *)(intptr_t)i);
    }
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
                        }, {
                            DIAL_RADIUS * 2.0, DIAL_RADIUS * 2.0
                        }
    };
}

void dial_nudge(int delta) {
    int next = ((int)gDialValue + delta % (int)DIAL_RANGE + (int)DIAL_RANGE * 64) % (int)DIAL_RANGE;

    gDialValue = (uint32_t)next;

    if (gSessionOpen) {
        midi_post_rotary_event(delta);
        midi_note_ui_activity();   // the drag's own 120 ms polling stops at release; this catches the landing point
        // No LCD request here — while a drag is active, the MIDI poll
        // thread already polls for a delta on its own throttled cadence
        // (see gDialDragActive), independent of individual ticks.
    }
    synthlib_request_redraw();
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

// ── Button layout (right-hand section) ───────────────────────────────────────
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
    {pkNumpadPlusMinus, {0}, "+/-",      false, false, 0},
};

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
// handle_mouse_button()'s inline hit-test (removed from mouseHandle.c).
//
// Split out of the click handler so the soft-key boxes drawn on the LCD, and the backdoor's BUTTON
// command, raise exactly the same event a click on the button itself does — including lighting the
// on-screen button, so pressing a box on the display visibly presses the F-key below it.
void emu_button_press(tButtonKey key, bool pressed) {
    tButton * btn = find_button(key);

    if (btn == NULL) {
        return;
    }
    LOG_DEBUG("hit button key=%d label=%s\n", (int)btn->key, btn->label);
    btn->pressed = pressed;

    // Stamped BEFORE the button event is posted, not after.
    //
    // Only the PRESS counts as activity — the release is the tail of the same gesture and changes
    // nothing further, but stamping it restarts the settle timer AND makes every reply in flight
    // look superseded.
    //
    // The ORDER matters because this is also what restarts the idle probe clock (see
    // eMsgCmdUiActivity). The MIDI thread can drain the queue and reach its polling decisions
    // between any two posts, so posting the button event first left a window in which an idle probe
    // went out on top of a press — and the whole frame the press actually wanted then had to queue
    // behind that probe's reply. Restarting the clock first closes the window: by the time the
    // button event reaches the wire, the poll has already been told to stand down.
    if (pressed) {
        midi_note_ui_activity();
    }
    midi_post_button_event(btn->key, pressed);

    // A WHOLE FRAME, not a delta and not a probe.
    //
    // Polling exists to answer "has the screen moved?". We just pressed a button, so it has —
    // there is nothing to find out, and the only open question is what it now shows, which only a
    // whole frame is allowed to answer (see LCD_USE_DELTAS). Asking for a delta here meant that
    // after LCD_RESYNC_IDLE_MS of quiet — an ordinary isolated press — the request went out as a
    // probe whose payload is deliberately discarded, with the frame we actually wanted following
    // behind it: two round trips on a 31250-baud link for one press, the second unable to start
    // until the first had finished.
    midi_post_lcd_refresh(true);
    synthlib_request_redraw();
}

static void button_click_handler(tCoord coord, eClickPhase phase, void * userData) {
    (void)coord;
    emu_button_press(((tButton *)userData)->key, phase == eClickPress);
}

static void render_button_at(tButtonKey key, double x, double y, double w, double h) {
    tButton * btn   = find_button(key);

    if (btn == NULL) {
        return;
    }
    btn->rectangle = (tRectangle){{
                                      x, y
                                  }, {
                                      w, h
                                  }
    };
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
    double                  ox          = LP_ORIGIN_X;
    double                  oy          = LP_ORIGIN_Y;

    // Clear stale hit rectangles for buttons not placed in this layout
    for (int i = 0; i < NUM_BUTTONS; i++) {
        gButtons[i].rectangle = (tRectangle){{
                                                 0.0, 0.0
                                             }, {
                                                 0.0, 0.0
                                             }
        };
    }

    // Row y positions
    double                  r0          = oy + LP_GAP;
    double                  r1          = r0 + LP_ROW;
    double                  r2          = r1 + LP_ROW;

    // ── Left panel: 2 rows × 10 columns ──────────────────────────────────────
    static const tButtonKey lp_row0[10] = {
        pkMaster, pkPresetManage, pkPresetEdit, pkAudition,
        pkF1,     pkF2,           pkF3,         pkF4,      pkF5, pkF6
    };
    static const tButtonKey lp_row1[10] = {
        pkDisk,    pkSampleManage, pkSampleEdit,
        pkAssign1, pkAssign2,      pkAssign3,
        pkExit,    pkPrev,         pkNext, pkEnter
    };

    // LP_COL_X() rather than a local expression: emu_softkey_rect() places the LCD's soft-key boxes
    // off the same macro, so the boxes cannot drift away from the F-keys they sit above.
    for (int c = 0; c < 10; c++) {
        double x = LP_COL_X(c);
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
        {pkNumpadPlusMinus, pkNumpad0, pkNumpadDot},
    };

    for (int nr = 0; nr < 4; nr++) {
        double ny = r1 + nr * LP_ROW;

        for (int nc = 0; nc < 3; nc++) {
            render_button_at(np[nr][nc], ox + RP_NP_X + nc * RP_NP_STR, ny, RP_NP_W, LP_H);
        }
    }
}

// ── Testing helpers (backdoor command channel) ───────────────────────────────

const tButton * emu_button_at_index(int index) {
    return ((index < 0) || (index >= NUM_BUTTONS)) ? NULL : &gButtons[index];
}

bool emu_button_lookup(const char * name, tButtonKey * keyOut) {
    if ((name == NULL) || (name[0] == '\0') || (keyOut == NULL)) {
        return false;
    }

    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (strcasecmp(gButtons[i].label, name) == 0) {
            *keyOut = gButtons[i].key;
            return true;
        }
    }

    // Fall back to a raw PEPTALK key code, for a button that has no on-screen label yet.
    char * end  = NULL;
    long   code = strtol(name, &end, 0);

    if ((end != NULL) && (*end == '\0') && (find_button((tButtonKey)code) != NULL)) {
        *keyOut = (tButtonKey)code;
        return true;
    }
    return false;
}

#ifdef __cplusplus
}
#endif
