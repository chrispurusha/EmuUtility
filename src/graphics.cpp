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
#include <ft2build.h>
#include FT_FREETYPE_H
#pragma clang diagnostic pop

#include "defs.h"
#include "synthlibDefs.h"
#include "types.h"
#include "globalVars.h"
#include "utils.h"
#include "utilsGraphics.h"
#include "emuGraphics.h"
#include "mouseHandle.h"
#include "menus.h"
#include "midiComms.h"
#include "misc.h"
#include "graphics.h"
#include "appMenuBar.h"
#include "synthlibHost.h"
#include "synthlibScale.h"
#include "synthlibPersistence.h"

static void setup_projection(GLFWwindow * win);

// ── GLFW callbacks ────────────────────────────────────────────────────────────

void framebuffer_size_callback(GLFWwindow * window, int width, int height) {
    (void)window;
    synthlib_scale_update(width, height);

    synthlib_request_redraw();
}

// Fires when the window moves to a display with a different HiDPI scale (e.g. dragging from a
// Retina built-in display to a non-Retina external one, or vice versa) — see synthlibScale.h's
// own comment for the bug this fixes (right-click menus and anything else deriving screen
// position from gGlobalGuiScale landing mispositioned wherever the real scale wasn't 2.0 — G2-Edit
// hit this first as issue #9; this app never had the fix at all until now).
static void content_scale_callback(GLFWwindow * window, float xscale, float yscale) {
    (void)yscale; // this app only ever uses a single uniform scale factor

    synthlib_scale_set_content_scale(window, xscale);

    synthlib_request_redraw();
}

void window_size_callback(GLFWwindow * window, int width, int height) {
    synthlib_save_window_size(width);
}

void window_pos_callback(GLFWwindow * window, int x, int y) {
    synthlib_save_window_pos(x, y);
}

void resize_window(int w, int h) {
    glfwSetWindowSize((GLFWwindow *)synthlib_window(), w, h);
}

void reposition_window(int x, int y) {
    glfwSetWindowPos((GLFWwindow *)synthlib_window(), x, y);
}

void window_close_callback(GLFWwindow * window) {
    synthlib_clear_redraw();

    glfwSetFramebufferSizeCallback((GLFWwindow *)synthlib_window(), NULL);
    glfwSetWindowContentScaleCallback((GLFWwindow *)synthlib_window(), NULL);
    glfwSetWindowCloseCallback((GLFWwindow *)synthlib_window(), NULL);
    glfwSetKeyCallback((GLFWwindow *)synthlib_window(), NULL);
    glfwSetCharCallback((GLFWwindow *)synthlib_window(), NULL);
    glfwSetCursorPosCallback((GLFWwindow *)synthlib_window(), NULL);
    glfwSetMouseButtonCallback((GLFWwindow *)synthlib_window(), NULL);
    glfwSetScrollCallback((GLFWwindow *)synthlib_window(), NULL);

    glfwSetWindowShouldClose((GLFWwindow *)synthlib_window(), GLFW_TRUE);
    glfwPostEmptyEvent();
}

void set_window_title(const char * filePath) {
    char         newTitle[100] = {0};
    const char * filename      = strrchr(filePath, '/');

    if (filename) {
        filename += 1;  // Skip the slash
    } else {
        filename = filePath;
    }
    snprintf(newTitle, sizeof(newTitle), "%s - %s", WINDOW_TITLE, filename);
    glfwSetWindowTitle((GLFWwindow *)synthlib_window(), newTitle);
}

void error_callback(int error, const char * description) {
    LOG_ERROR("GLFW error [%d]: %s\n", error, description);
}

static void window_refresh_cb(GLFWwindow * win) {
    (void)win;
    synthlib_request_redraw();
}

static void mouse_button_cb(GLFWwindow * win, int button, int action, int mods) {
    double x = 0.0;
    double y = 0.0;

    glfwGetCursorPos(win, &x, &y);
    handle_mouse_button(win, button, action, mods, x, y);
    synthlib_request_redraw();
}

static void cursor_pos_cb(GLFWwindow * win, double x, double y) {
    handle_cursor_pos(win, x, y);
}

static void key_cb(GLFWwindow * win, int key, int scancode, int action, int mods) {
    handle_key(win, key, scancode, action, mods);
    synthlib_request_redraw();
}

static void scroll_cb(GLFWwindow * win, double dx, double dy) {
    handle_scroll(win, dx, dy);
    synthlib_request_redraw();
}

// ── Wake (called from MIDI thread) ───────────────────────────────────────────

void wake_glfw(void) {
    glfwPostEmptyEvent();
}

// ── Setup co-ordinate system ──────────────────────────────────────────────────

static void setup_projection(GLFWwindow * win) {
    int fbW  = 0;
    int fbH  = 0;

    glfwGetFramebufferSize(win, &fbW, &fbH);

    int winW = 0;
    int winH = 0;
    glfwGetWindowSize(win, &winW, &winH);

    //gGlobalGuiScale = (winW > 0) ? (double)fbW / (double)winW : 1.0;

    set_render_width(fbW);
    set_render_height(fbH);

    glViewport(0, 0, fbW, fbH);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, fbW, fbH, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}

// ── Font (FreeType via system path) ──────────────────────────────────────────

static int init_font(void) {
    static const char * fontPaths[] = {"/System/Library/Fonts/Supplemental/Arial.ttf",
                                       "/System/Library/Fonts/Helvetica.ttc",
                                       "/System/Library/Fonts/SFNSMono.ttf",
                                       NULL};

    for (int i = 0; fontPaths[i] != NULL; i++) {
        if (preload_glyph_textures(fontPaths[i], 72.0)) {
            LOG_DEBUG("Loaded font: %s\n", fontPaths[i]);
            return EXIT_SUCCESS;
        }
    }

    LOG_ERROR("Could not load any system font\n");
    return EXIT_FAILURE;
}

// ── init_graphics ─────────────────────────────────────────────────────────────

void init_graphics(void) {
    char title[128] = {0};

    snprintf(title, sizeof(title), "%s - Build %s %s", WINDOW_TITLE, __DATE__, __TIME__);

    // Injection point for the mouse-coord query every SynthLib popup/panel file (contextMenu.c,
    // menuBar.c, alertDialog.cpp, bankBrowser.cpp, fileBrowser.cpp) needs — see synthlibHost.h's
    // own comment.
    synthlib_host_init((tSynthLibHost){
        .mouseCoord = get_global_gui_scaled_mouse_coord,
    });
    synthlib_scale_init(TARGET_FRAME_BUFF_WIDTH);

    glfwSetErrorCallback(error_callback);

    if (!glfwInit()) {
        exit(EXIT_FAILURE);
    }
    //register_glfw_wake_cb(wake_glfw);
    //register_full_patch_change_notify_cb(notify_full_patch_change);
    //topbar_init_controls();

    glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_TRUE);
    glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, GLFW_TRUE);  // Needed for Intel systems with discrete graphics
    synthlib_set_window((void *)glfwCreateWindow(TARGET_FRAME_BUFF_WIDTH / 4, TARGET_FRAME_BUFF_HEIGHT / 4, title, NULL, NULL));

    if (!synthlib_window()) {
        glfwTerminate();
        exit(EXIT_FAILURE);
    }
    // Minimum 640x360 (TARGET/4, so still exactly the locked 16:9). The old TARGET/8 allowed a
    // 320pt window, which on a 1x display is a 320px framebuffer — gGlobalGuiScale 0.25, putting
    // body text at ~3px, unreadable however well it is rendered.
    glfwSetWindowSizeLimits((GLFWwindow *)synthlib_window(), TARGET_FRAME_BUFF_WIDTH / 4, TARGET_FRAME_BUFF_HEIGHT / 4, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowAspectRatio((GLFWwindow *)synthlib_window(), TARGET_FRAME_BUFF_WIDTH, TARGET_FRAME_BUFF_HEIGHT);

    glfwMakeContextCurrent((GLFWwindow *)synthlib_window());

    // Real initial scale for whichever display the window opens on, not the 2.0 (Retina-only)
    // assumption this used to hardcode — see synthlibScale.h's own comment for the bug that broke
    // (G2-Edit hit this first as issue #9; this app never had the fix at all until now).
    synthlib_scale_query_initial(synthlib_window());

    {
        int fbWidth  = 0;
        int fbHeight = 0;
        glfwGetFramebufferSize((GLFWwindow *)synthlib_window(), &fbWidth, &fbHeight);
        framebuffer_size_callback((GLFWwindow *)synthlib_window(), fbWidth, fbHeight);
    }

    glfwSetFramebufferSizeCallback((GLFWwindow *)synthlib_window(), framebuffer_size_callback);
    glfwSetWindowContentScaleCallback((GLFWwindow *)synthlib_window(), content_scale_callback);
    glfwSetWindowSizeCallback((GLFWwindow *)synthlib_window(), window_size_callback);
    glfwSetWindowPosCallback((GLFWwindow *)synthlib_window(), window_pos_callback);
    glfwSwapInterval(1);
    glfwSetWindowCloseCallback((GLFWwindow *)synthlib_window(), window_close_callback);
    glfwSetKeyCallback((GLFWwindow *)synthlib_window(), key_cb);
    glfwSetCursorPosCallback((GLFWwindow *)synthlib_window(), cursor_pos_cb);
    glfwSetMouseButtonCallback((GLFWwindow *)synthlib_window(), mouse_button_cb);
    glfwSetScrollCallback((GLFWwindow *)synthlib_window(), scroll_cb);
    glfwSetWindowRefreshCallback((GLFWwindow *)synthlib_window(), window_refresh_cb);

    glEnable(GL_BLEND); // TODO - Assess if G2 edit could benefit from this
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    init_font();        // TODO - G2 edit could benefit from this if we're loading multiple fonts
    init_lcd_texture();

    register_midi_wake_cb(wake_glfw);     // TODO - this doesn't belong in here
}
// ── Render frame ──────────────────────────────────────────────────────────────

static void render_frame(GLFWwindow * win) {
    setup_projection(win);

    glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    clear_click_regions();

    //double     logW     = /*(double)get_render_width() /*/ gGlobalGuiScale;
    //double     logH     = TARGET_FRAME_BUFF_HEIGHT;

    // LCD area: 2× the raw 240×64 pixel size, centred in left half of virtual space
    //double     lcdDispW = LCD_WIDTH * 2.0;
    //double     lcdDispH = LCD_HEIGHT * 2.0;
    //double     lcdX     = (logW  - lcdDispW);
    //double     lcdY     = 10.0;

    //tRectangle mainArea = {{0.0, 0.0}, {logW, logH}};
    //tRectangle lcdArea  = {{lcdX, lcdY}, {lcdDispW, lcdDispH}};
    //tRectangle btnArea  = {{0.0, lcdY + lcdDispH + LCD_BORDER + 8.0}, {logW, button_panel_height(logW)}};

    render_lcd();
    render_dial_knob();
    render_button_panel();
    render_menu_bar(gAppMenuBar, app_menu_bar_rect());
    render_context_menu();

    glfwSwapBuffers(win);
}

// ── do_graphics_loop ──────────────────────────────────────────────────────────

void do_graphics_loop(void) {
    GLFWwindow * win = (GLFWwindow *)synthlib_window();

    while (!synthlib_quit_requested() && !glfwWindowShouldClose(win)) {
        // Polled every tick (not just on cursor move) so a hover-dwell timer elapses even while
        // the mouse sits still, and so switching from one open top-level menu-bar label to
        // another happens on hover, not just a second click — matches G2-Edit/graphics.cpp and
        // SynthEdit/graphics.cpp. Needs glfwWaitEventsTimeout() below rather than
        // glfwWaitEvents()'s indefinite block for the same reason.
        update_context_menu_hover();
        update_menu_bar_hover(gAppMenuBar, app_menu_bar_rect());

        bool reDraw = synthlib_consume_redraw();

        if (reDraw) {
            render_frame(win);
        }
        glfwWaitEventsTimeout(0.05);
    }
}

// ── clean_up_graphics ─────────────────────────────────────────────────────────

void clean_up_graphics(void) {
    free_textures();

    glfwDestroyWindow((GLFWwindow *)synthlib_window());
    synthlib_set_window(NULL);
    glfwTerminate();
}

#ifdef __cplusplus
}
#endif
