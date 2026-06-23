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

#include "defs.h"
#include "types.h"
#include "globalVars.h"
#include "emuGraphics.h"
#include "peptalk.h"
#include "menus.h"
#include "utils.h"
#include "mouseHandle.h"

// Convert GLFW window-space (x,y) to logical canvas coordinates.
static tCoord window_to_logical(void * win, double x, double y) {
    int    winW  = 0;
    int    winH  = 0;

    // glfwGetWindowSize signature matches: (GLFWwindow*, int*, int*)
    // Cast via void* to avoid pulling GLFW header into this C file.
    typedef void (*GetWinSizeFn)(void *, int *, int *);
    extern void glfwGetWindowSize(void *, int *, int *);
    glfwGetWindowSize(win, &winW, &winH);

    tCoord coord = {
        .x = (winW > 0) ? (x / winW) * TARGET_FRAME_BUFF_WIDTH : x,
        .y = (winH > 0) ? (y / winH) * TARGET_FRAME_BUFF_HEIGHT : y,
    };
    return coord;
}

void handle_mouse_button(void * win, int button, int action, int mods, double x, double y) {
    (void)mods;

    if (button != 0) {   // left button only
        return;
    }
    tCoord    coord   = window_to_logical(win, x, y);
    bool      pressed = (action == 1);   // GLFW_PRESS == 1

    LOG_DEBUG("mouse %s win(%.0f,%.0f) logical(%.0f,%.0f)\n",
              pressed ? "press" : "release", x, y, coord.x, coord.y);

    if (close_context_menu_if_outside(coord)) {
        return;
    }

    if (handle_context_menu_click(coord)) {
        return;
    }
    tButton * btn     = button_at(coord);

    if (btn != NULL) {
        LOG_DEBUG("hit button key=%d label=%s\n", (int)btn->key, btn->label);
        btn->pressed = pressed;
        peptalk_send_button_event(btn->key, pressed);
        atomic_store(&gNeedLcdDelta, true);
        atomic_store(&gReDraw, true);
    } else {
        LOG_DEBUG("no button at logical(%.0f,%.0f)\n", coord.x, coord.y);
    }
}

void handle_cursor_pos(void * win, double x, double y) {
    (void)win;
    (void)x;
    (void)y;
}

void handle_key(void * win, int key, int scancode, int action, int mods) {
    (void)win;
    (void)scancode;
    (void)mods;

    if (action == 0) {   // GLFW_RELEASE
        return;
    }
    // Basic keyboard → PEPTALK button mapping
    tButtonKey bk    = (tButtonKey)0;
    bool       found = true;

    switch (key) {
        case 256: bk   = pkExit;
            break;                        // GLFW_KEY_ESCAPE
        case 257: bk   = pkEnter;
            break;                        // GLFW_KEY_ENTER
        case 265: bk   = pkUp;
            break;                        // GLFW_KEY_UP
        case 264: bk   = pkDown;
            break;                        // GLFW_KEY_DOWN
        case 263: bk   = pkLeft;
            break;                        // GLFW_KEY_LEFT
        case 262: bk   = pkRight;
            break;                        // GLFW_KEY_RIGHT
        case 280: bk   = pkPrev;
            break;                        // GLFW_KEY_PAGE_UP
        case 281: bk   = pkNext;
            break;                        // GLFW_KEY_PAGE_DOWN
        case 290: bk   = pkF1;
            break;                        // GLFW_KEY_F1
        case 291: bk   = pkF2;
            break;
        case 292: bk   = pkF3;
            break;
        case 293: bk   = pkF4;
            break;
        case 294: bk   = pkF5;
            break;
        case 295: bk   = pkF6;
            break;
        case 45:  bk   = pkDec;
            break;                        // GLFW_KEY_MINUS
        case 61:  bk   = pkInc;
            break;                        // GLFW_KEY_EQUAL
        default: found = false;
            break;
    }

    if (found) {
        peptalk_send_button_event(bk, true);
        peptalk_send_button_event(bk, false);
        atomic_store(&gNeedLcdDelta, true);
        atomic_store(&gReDraw, true);
    }
}

void handle_scroll(void * win, double dx, double dy) {
    (void)win;
    (void)dx;

    if (dy == 0.0) {
        return;
    }
    int delta = (int)(dy * 3.0);
    peptalk_send_rotary_event(delta);
    atomic_store(&gNeedLcdDelta, true);
    atomic_store(&gReDraw, true);
}
