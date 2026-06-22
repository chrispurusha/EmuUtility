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

#ifdef __cplusplus
extern "C" {
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define GL_SILENCE_DEPRECATION 1
#include <GLFW/glfw3.h>
#pragma clang diagnostic pop

#include "defs.h"
#include "types.h"
#include "globalVars.h"
#include "utilsGraphics.h"
#include "emuGraphics.h"

static GLuint gLcdTexture    = 0;
static uint32_t gLastRefresh = 0xFFFFFFFF;

// ── LCD texture ───────────────────────────────────────────────────────────────

void init_lcd_texture(void) {
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
            int   pixelIdx = (byte * 8) + (7 - bit);
            int   rgbaIdx  = pixelIdx * 4;
            bool  lit      = (b >> bit) & 1;
            const tRgb * col = lit ? &fg : &bg;

            rgba[rgbaIdx + 0] = (uint8_t)(col->red   * 255.0);
            rgba[rgbaIdx + 1] = (uint8_t)(col->green * 255.0);
            rgba[rgbaIdx + 2] = (uint8_t)(col->blue  * 255.0);
            rgba[rgbaIdx + 3] = 255;
        }
    }

    glBindTexture(GL_TEXTURE_2D, gLcdTexture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, LCD_WIDTH, LCD_HEIGHT, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glBindTexture(GL_TEXTURE_2D, 0);
    gLastRefresh = gLcd.refresh;
}

void render_lcd(tRectangle area) {
    if (gLcdTexture == 0) {
        return;
    }

    if (gLcd.refresh != gLastRefresh) {
        update_lcd_texture();
    }

    double x = area.coord.x;
    double y = area.coord.y;
    double w = area.size.w;
    double h = area.size.h;

    // Background fill
    set_rgb_colour((tRgb)RGB_LCD_BG);
    render_rectangle(mainArea, (tRectangle){{x, y}, {w, h}});

    if (!atomic_load(&gSessionOpen)) {
        return;
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, gLcdTexture);
    glColor3f(1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS);
    glTexCoord2f(0.0f, 0.0f); glVertex2d(x,     y);
    glTexCoord2f(1.0f, 0.0f); glVertex2d(x + w, y);
    glTexCoord2f(1.0f, 1.0f); glVertex2d(x + w, y + h);
    glTexCoord2f(0.0f, 1.0f); glVertex2d(x,     y + h);
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

// ── Button layout ─────────────────────────────────────────────────────────────
// Matches the physical E4/E5000 front panel layout.

#define BTN_W   52.0
#define BTN_H   20.0
#define BTN_GAP 4.0
#define BTN_ROW (BTN_H + BTN_GAP)

static tButton gButtons[] = {
    // Row 0 — main section buttons
    {pkPresetManage, {0}, "Preset",    false, false, 0},
    {pkSampleManage, {0}, "Sample",    false, false, 0},
    {pkPresetEdit,   {0}, "Prs Edit",  false, false, 0},
    {pkSampleEdit,   {0}, "Smp Edit",  false, false, 0},
    {pkMaster,       {0}, "Master",    false, false, 0},
    {pkDisk,         {0}, "Disk",      false, false, 0},
    // Row 0 continued — sequence controls
    {pkSequencer,    {0}, "Seqcr",     false, false, 0},
    {pkSeqRtz,       {0}, "RTZ",       false, false, 0},
    {pkSeqRew,       {0}, "Rew",       false, false, 0},
    {pkSeqFfwd,      {0}, "FFwd",      false, false, 0},
    {pkSeqStop,      {0}, "Stop",      false, false, 0},
    {pkSeqPlay,      {0}, "Play",      false, false, 0},
    {pkSeqRec,       {0}, "Rec",       false, false, 0},

    // Row 1 — assign / function keys
    {pkAssign1,      {0}, "Assign 1",  false, false, 0},
    {pkAssign2,      {0}, "Assign 2",  false, false, 0},
    {pkAssign3,      {0}, "Assign 3",  false, false, 0},
    {pkF1,           {0}, "F1",        false, false, 0},
    {pkF2,           {0}, "F2",        false, false, 0},
    {pkF3,           {0}, "F3",        false, false, 0},
    {pkF4,           {0}, "F4",        false, false, 0},
    {pkF5,           {0}, "F5",        false, false, 0},
    {pkF6,           {0}, "F6",        false, false, 0},

    // Row 2 — navigation and numeric
    {pkPrev,         {0}, "Prev",      false, false, 0},
    {pkNext,         {0}, "Next",      false, false, 0},
    {pkUp,           {0}, "Up",        false, false, 0},
    {pkDown,         {0}, "Down",      false, false, 0},
    {pkLeft,         {0}, "Left",      false, false, 0},
    {pkRight,        {0}, "Right",     false, false, 0},
    {pkEnter,        {0}, "Enter",     false, false, 0},
    {pkExit,         {0}, "Exit",      false, false, 0},
    {pkDec,          {0}, "Dec",       false, false, 0},
    {pkInc,          {0}, "Inc",       false, false, 0},
    {pkAudition,     {0}, "Audition",  false, false, 0},
    {pkControlsFx,   {0}, "Ctrl/FX",   false, false, 0},

    // Row 3 — numeric pad
    {pkNumpad1,      {0}, "1",         false, false, 0},
    {pkNumpad2,      {0}, "2",         false, false, 0},
    {pkNumpad3,      {0}, "3",         false, false, 0},
    {pkNumpad4,      {0}, "4",         false, false, 0},
    {pkNumpad5,      {0}, "5",         false, false, 0},
    {pkNumpad6,      {0}, "6",         false, false, 0},
    {pkNumpad7,      {0}, "7",         false, false, 0},
    {pkNumpad8,      {0}, "8",         false, false, 0},
    {pkNumpad9,      {0}, "9",         false, false, 0},
    {pkNumpad0,      {0}, "0",         false, false, 0},
    {pkNumpadDot,    {0}, ".",         false, false, 0},
    {pkNumpadPlusMinus, {0}, "+/-",    false, false, 0},
};

static const int NUM_BUTTONS = (int)(sizeof(gButtons) / sizeof(gButtons[0]));

double button_panel_height(void) {
    return (BTN_ROW * 4) + BTN_GAP;
}

void render_button_panel(tRectangle area) {
    double startX  = area.coord.x + BTN_GAP;
    double startY  = area.coord.y + BTN_GAP;
    double cols    = 13.0;
    double x       = startX;
    double y       = startY;
    int    rowCount = 0;
    int    colCount = 0;

    // Simple grid layout: 13 per row
    for (int i = 0; i < NUM_BUTTONS; i++) {
        tButton * btn = &gButtons[i];

        btn->rectangle = (tRectangle){{x, y}, {BTN_W, BTN_H}};

        uint32_t leds  = atomic_load(&gLeds);
        bool     ledOn = btn->hasLed && (leds & (1u << btn->ledIndex));
        tRgb     col   = btn->pressed ? (tRgb)RGB_AMBER : ledOn ? (tRgb)RGB_GREEN_ON : (tRgb)RGB_GREY_3;

        set_rgb_colour(col);
        render_rectangle(mainArea, btn->rectangle);
        set_rgb_colour((tRgb)RGB_WHITE);
        render_text(mainArea, (tRectangle){{x + 2.0, y + 5.0}, {0.0, 9.0}}, (char *)btn->label);

        x += BTN_W + BTN_GAP;
        colCount++;

        if (colCount >= (int)cols) {
            colCount = 0;
            x        = startX;
            y       += BTN_ROW;
            rowCount++;
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
