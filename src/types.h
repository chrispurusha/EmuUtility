/*
 * The EmuUtility application.
 *
 * Copyright (C) 2025 Chris Turner <chris_purusha@icloud.com>
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

#ifndef __TYPES_H__
#define __TYPES_H__

#include <stdint.h>
#include <stdbool.h>
#include "defs.h"

// ── Geometry ────────────────────────────────────────────────────────────────

typedef struct {
    double x;
    double y;
} tCoord;

typedef struct {
    double w;
    double h;
} tSize;

typedef struct {
    tCoord coord;
    tSize  size;
} tRectangle;

// ── Colour ──────────────────────────────────────────────────────────────────

typedef struct {
    double red;
    double green;
    double blue;
} tRgb;

#define RGB_BLACK              {0.00, 0.00, 0.00}
#define RGB_WHITE              {1.00, 1.00, 1.00}
#define RGB_BACKGROUND_GREY    {0.30, 0.30, 0.30}
#define RGB_GREY_2             {0.20, 0.20, 0.20}
#define RGB_GREY_3             {0.30, 0.30, 0.30}
#define RGB_GREY_5             {0.50, 0.50, 0.50}
#define RGB_GREY_7             {0.70, 0.70, 0.70}
#define RGB_GREEN_ON           {0.00, 0.80, 0.00}
#define RGB_AMBER              {1.00, 0.60, 0.00}
#define RGB_LCD_BG             {0.18, 0.22, 0.18}  // dark green, like E-mu LCD
#define RGB_LCD_FG             {0.70, 0.85, 0.60}  // lit pixel colour

// ── PEPTALK button key codes ─────────────────────────────────────────────────

typedef enum {
    pkSeqRtz          = 81,
    pkSeqRew          = 82,
    pkSeqFfwd         = 83,
    pkSeqStop         = 84,
    pkSeqPlay         = 85,
    pkSeqRec          = 86,
    pkSequencer       = 87,
    pkPresetManage    = 88,
    pkSampleManage    = 89,
    pkPresetEdit      = 90,
    pkSampleEdit      = 91,
    pkMaster          = 92,
    pkDisk            = 93,
    pkExit            = 94,
    pkAssign1         = 95,
    pkAssign2         = 96,
    pkF1 = 98,
    pkAssign3         = 99,
    pkF2 = 100,
    pkAudition        = 101,
    pkF3 = 102,
    pkControlsFx      = 103,
    pkF4 = 104,
    pkPrev            = 105,
    pkF5 = 106,
    pkNext            = 107,
    pkF6 = 108,
    pkEnter           = 109,
    pkUp = 110,
    pkLeft            = 111,
    pkRight           = 112,
    pkDown            = 113,
    pkDec = 114,
    pkInc = 115,
    pkNumpad1         = 116,
    pkNumpad2         = 117,
    pkNumpad3         = 118,
    pkNumpad4         = 119,
    pkNumpad5         = 120,
    pkNumpad6         = 121,
    pkNumpad7         = 122,
    pkNumpad8         = 123,
    pkNumpad9         = 124,
    pkNumpadPlusMinus = 125,
    pkNumpad0         = 126,
    pkNumpadDot       = 127,
} tButtonKey;

// ── E-mu device info ─────────────────────────────────────────────────────────

typedef struct {
    uint8_t  id;           // PEPTALK device ID (from identity response)
    uint16_t family;       // MIDI identity family code
    uint16_t member;       // MIDI identity member code
    char     model[32];    // human-readable model string
    char     firmware[8];  // firmware version string
    bool     connected;
} tEmuDevice;

// ── LCD display buffer ───────────────────────────────────────────────────────

typedef struct {
    uint8_t  pixels[LCD_BYTES];   // 240×64, 1 bit per pixel, packed MSB-first
    uint32_t refresh;             // incremented on each update, triggers redraw
} tLcdBuffer;

// ── On-screen button ─────────────────────────────────────────────────────────

typedef struct {
    tButtonKey   key;
    tRectangle   rectangle;
    const char * label;
    bool         pressed;
    bool         hasLed;
    uint8_t      ledIndex;
} tButton;

// ── Graphics primitives (used by utilsGraphics) ──────────────────────────────

typedef struct {
    tCoord coord1;
    tCoord coord2rel;
    tCoord coord3rel;
} tTriangle;

typedef struct {
    double u1;
    double v1;
    double u2;
    double v2;
    double advance_x;
    int    width;
    int    height;
    int    offset_x;
    int    offset_y;
} GlyphInfo;

typedef enum {
    eNoCache,
    eCache,
} tCache;

typedef enum {
    mainArea,
    moduleArea,
} tArea;

// ── RGBA colour (alpha) ───────────────────────────────────────────────────────

typedef struct {
    double red;
    double green;
    double blue;
    double alpha;
} tRgba;

#endif // __TYPES_H__
