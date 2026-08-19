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

#include <unistd.h>
#include <ctype.h>

// stb_image_write is already bundled as a GLFW build dependency — reused here (rather than a second
// PNG library) purely for the backdoor's SCREENSHOT and LCDDUMP commands. Same arrangement as
// G2-Edit/src/graphics.c.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../SynthLib/ThirdParty/glfw/deps/stb_image_write.h"
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
#include "noteEntry.h"

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

// The key-up half of a held key goes to whoever has focus, so a note started here and finished in
// another application would never be released — and a stuck note on a real sampler keeps sounding
// until something else stops it.
static void window_focus_cb(GLFWwindow * win, int focused) {
    (void)win;

    if (focused == GLFW_FALSE) {
        note_entry_all_notes_off();
    }
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
    static const char * fontPaths[] = {
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/System/Library/Fonts/SFNSMono.ttf",
        NULL
    };

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
    glfwSetWindowFocusCallback((GLFWwindow *)synthlib_window(), window_focus_cb);
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

// ── Backdoor command channel (testing only) ───────────────────────────────────
// A tiny file-driven command channel so a scripted test can drive this app and read back what it
// drew — press a front-panel button, nudge the dial, capture the window, or dump the raw LCD
// bitmap — without a real mouse click. Ported from G2-Edit's and SynthEdit's proven mechanism.
//
// GATED behind the EMU_UTILITY_BACKDOOR environment variable: unset (the owner's normal
// double-click launch) => completely inert, and the idle loop keeps its normal cadence. Set (a test
// launch from a shell) => a command file is honoured each tick. The gate matters more here than in
// SynthEdit, because BUTTON and DIAL send real PEPTALK events to a real connected sampler.
//
// This app is sandboxed (com.apple.security.app-sandbox), so a hardcoded "/tmp/..." path would be
// silently unreachable — fopen() on one just returns NULL, no error, no crash. The paths are built
// on emu_temp_dir() (misc.h), this app's own container tmp folder, instead.
//
// Command file (<container tmp>/emuutil_cmd.txt): one command per file, first line only,
// "<COMMAND> <arg>". The result ("OK\n" / "ERROR: ...\n", or the command's own text) is written to
// <container tmp>/emuutil_result.txt and the command file is deleted, so a caller polls for the
// command file's disappearance to know it is done.
//   SCREENSHOT <path>  — synchronous render_frame() then glReadPixels + PNG of the whole window
//   LCDDUMP [path]     — the raw 240x64 LCD bitmap as an ASCII grid ('#' lit, '.' unlit) in the
//                        result file, and, if a path is given, also as a 1:1 240x64 PNG. This is
//                        what lets the soft-key box geometry be measured in DEVICE pixels rather
//                        than guessed off a scaled screenshot.
//   BUTTON <label|code> [down|up] — a front-panel button, by its on-screen label ("F1", "Prs Edit",
//                        case-insensitive) or by its raw PEPTALK key code. With no third word it
//                        does a press AND a release, which is what a real click does.
//   DIAL <delta>       — turn the data wheel by that many detents (+ve clockwise)
//   KEY <char> [down|up] — deliver a key to handle_key() as GLFW would, so the computer-keyboard
//                        note entry and the keyboard-to-panel shortcuts can be exercised headlessly.
//                        A single character ('a', 'z') or a raw GLFW key code. With no third word it
//                        does a press AND a release.
//   REFRESH            — ask the device for a full LCD dump on the next poll
//   STATE              — session/device state plus the on-screen geometry: the LCD rectangle, the
//                        six soft-key rectangles, and every button's rectangle
static bool backdoor_enabled(void) {
    static int cached = -1;

    if (cached < 0) {
        const char * v = getenv("EMU_UTILITY_BACKDOOR");
        cached = ((v != NULL) && (v[0] != '\0')) ? 1 : 0;
    }
    return cached == 1;
}

static const char * backdoor_cmd_path(void) {
    static char path[1088];

    snprintf(path, sizeof(path), "%semuutil_cmd.txt", emu_temp_dir());
    return path;
}

static const char * backdoor_result_path(void) {
    static char path[1088];

    snprintf(path, sizeof(path), "%semuutil_result.txt", emu_temp_dir());
    return path;
}

static void backdoor_write_result(const char * text) {
    FILE * f = fopen(backdoor_result_path(), "w");

    if (f != NULL) {
        fputs(text, f);
        fclose(f);
    }
}

static void backdoor_screenshot(GLFWwindow * win, const char * path) {
    render_frame(win); // synchronous, so the capture reflects the command that just ran, not a stale frame

    int       w      = get_render_width();
    int       h      = get_render_height();

    if ((w <= 0) || (h <= 0)) {
        backdoor_write_result("ERROR: zero-size framebuffer\n");
        return;
    }
    uint8_t * pixels = (uint8_t *)malloc((size_t)w * (size_t)h * 3);

    if (pixels == NULL) {
        backdoor_write_result("ERROR: out of memory\n");
        return;
    }
    // Tightly-packed rows. Without this, glReadPixels' default GL_PACK_ALIGNMENT of 4 pads each row
    // whenever w*3 isn't a multiple of 4, which both shears the PNG and overruns the buffer.
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    stbi_flip_vertically_on_write(1); // GL origin is bottom-left; PNGs are top-down

    int       ok     = stbi_write_png(path, w, h, 3, pixels, w * 3);

    free(pixels);
    backdoor_write_result(ok ? "OK\n" : "ERROR: stbi_write_png failed\n");
}

// The LCD as the device actually sends it: 240x64, one character per pixel. Written straight to the
// result file rather than composed in a buffer, because the grid alone is 64 * 241 bytes.
static void backdoor_lcd_dump(const char * pngPath) {
    uint8_t pixels[LCD_BYTES];

    pthread_mutex_lock(&gLcdMutex);
    memcpy(pixels, gLcd.pixels, LCD_BYTES);
    pthread_mutex_unlock(&gLcdMutex);

    FILE *  file = fopen(backdoor_result_path(), "w");

    if (file == NULL) {
        return;
    }
    fprintf(file, "OK\nlcd=%dx%d\n", LCD_WIDTH, LCD_HEIGHT);

    for (int y = 0; y < LCD_HEIGHT; y++) {
        for (int x = 0; x < LCD_WIDTH; x++) {
            int idx = (y * LCD_WIDTH) + x;

            fputc(((pixels[idx / 8] >> (7 - (idx % 8))) & 1) ? '#' : '.', file);
        }

        fputc('\n', file);
    }

    if ((pngPath != NULL) && (pngPath[0] != '\0')) {
        static uint8_t rgb[LCD_WIDTH * LCD_HEIGHT * 3];

        for (int idx = 0; idx < (LCD_WIDTH * LCD_HEIGHT); idx++) {
            bool lit = ((pixels[idx / 8] >> (7 - (idx % 8))) & 1) != 0;

            rgb[idx * 3 + 0] = lit ? 0 : 255;
            rgb[idx * 3 + 1] = lit ? 0 : 255;
            rgb[idx * 3 + 2] = lit ? 0 : 255;
        }

        stbi_flip_vertically_on_write(0); // this buffer is already top-down, unlike a glReadPixels one
        fprintf(file, "png=%s %s\n", pngPath,
                stbi_write_png(pngPath, LCD_WIDTH, LCD_HEIGHT, 3, rgb, LCD_WIDTH * 3) ? "ok" : "FAILED");
    }
    fclose(file);
}

static void backdoor_dump_state(char * out, size_t outMax) {
    size_t     used = 0;

    used += (size_t)snprintf(out + used, outMax - used,
                             "OK\nsession=%s connected=%s deviceId=0x%02X family=%u member=%u\n",
                             gSessionOpen ? "open" : "closed",
                             gDevice.connected ? "yes" : "no",
                             gDevice.id, (unsigned)gDevice.family, (unsigned)gDevice.member);

    used += (size_t)snprintf(out + used, outMax - used, "noteEntryFirstNote=%u\n",
                             (unsigned)note_entry_first_note());

    tRectangle lcd  = emu_lcd_rect();

    used += (size_t)snprintf(out + used, outMax - used, "lcd rect=%.1f,%.1f %.1fx%.1f refresh=%u\n",
                             lcd.coord.x, lcd.coord.y, lcd.size.w, lcd.size.h, (unsigned)gLcd.refresh);

    for (int i = 0; (i < EMU_SOFTKEY_COUNT) && (used < outMax); i++) {
        tRectangle r = emu_softkey_rect(i);

        used += (size_t)snprintf(out + used, outMax - used, "softkey %d F%d rect=%.1f,%.1f %.1fx%.1f\n",
                                 i, i + 1, r.coord.x, r.coord.y, r.size.w, r.size.h);
    }

    for (int i = 0; used < outMax; i++) {
        const tButton * btn = emu_button_at_index(i);

        if (btn == NULL) {
            break;
        }
        used += (size_t)snprintf(out + used, outMax - used, "button %-9s key=%3d rect=%.1f,%.1f %.1fx%.1f\n",
                                 btn->label, (int)btn->key,
                                 btn->rectangle.coord.x, btn->rectangle.coord.y,
                                 btn->rectangle.size.w, btn->rectangle.size.h);
    }
}

static void backdoor_dispatch(const char * cmd, const char * arg, GLFWwindow * win) {
    if (strcmp(cmd, "SCREENSHOT") == 0) {
        backdoor_screenshot(win, arg);
    } else if (strcmp(cmd, "LCDDUMP") == 0) {
        backdoor_lcd_dump(arg);
    } else if (strcmp(cmd, "BUTTON") == 0) {
        char       name[32] = {0};
        char       phase[8] = {0};
        int        parsed   = sscanf(arg, "%31s %7s", name, phase);
        tButtonKey key      = 0;

        if (parsed < 1) {
            backdoor_write_result("ERROR: expected 'BUTTON <label|code> [down|up]'\n");
            return;
        }

        if (!emu_button_lookup(name, &key)) {
            char msg[128];

            snprintf(msg, sizeof(msg), "ERROR: no button '%s'\n", name);
            backdoor_write_result(msg);
            return;
        }
        // No third word means a full click: the device acts on the press, but leaving it held would
        // leave the on-screen button stuck amber and the sampler seeing a key held down forever.
        bool       down     = (parsed < 2) || (strcasecmp(phase, "down") == 0);
        bool       up       = (parsed < 2) || (strcasecmp(phase, "up") == 0);

        if (down) {
            emu_button_press(key, true);
        }

        if (up) {
            emu_button_press(key, false);
        }
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "DIAL") == 0) {
        int delta = 0;

        if (sscanf(arg, "%d", &delta) != 1) {
            backdoor_write_result("ERROR: expected 'DIAL <delta>'\n");
            return;
        }
        dial_nudge(delta);
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "KEY") == 0) {
        char name[32] = {0};
        char phase[8] = {0};
        int  parsed   = sscanf(arg, "%31s %7s", name, phase);
        int  glfwKey  = 0;

        if (parsed < 1) {
            backdoor_write_result("ERROR: expected 'KEY <char|code> [down|up]'\n");
            return;
        }

        if (name[1] == '\0') {
            // GLFW's printable key codes ARE the uppercase ASCII values, which is also why the map
            // in noteEntry.c is written with GLFW_KEY_A rather than a scancode.
            glfwKey = toupper((unsigned char)name[0]);
        } else if (sscanf(name, "%d", &glfwKey) != 1) {
            backdoor_write_result("ERROR: expected 'KEY <char|code> [down|up]'\n");
            return;
        }
        bool down     = (parsed < 2) || (strcasecmp(phase, "down") == 0);
        bool up       = (parsed < 2) || (strcasecmp(phase, "up") == 0);

        if (down) {
            handle_key(win, glfwKey, 0, GLFW_PRESS, 0);
        }

        if (up) {
            handle_key(win, glfwKey, 0, GLFW_RELEASE, 0);
        }
        synthlib_request_redraw();
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "REFRESH") == 0) {
        gNeedLcdFull  = true;
        gNeedLcdDelta = false;
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "STATE") == 0) {
        char dump[8192];

        backdoor_dump_state(dump, sizeof(dump));
        backdoor_write_result(dump);
    } else {
        char msg[128];

        snprintf(msg, sizeof(msg), "ERROR: unknown command '%s'\n", cmd);
        backdoor_write_result(msg);
    }
}

static void backdoor_poll(GLFWwindow * win) {
    if (!backdoor_enabled()) {
        return;
    }
    const char * cmdPath   = backdoor_cmd_path();

    if (access(cmdPath, F_OK) != 0) {
        return;
    }
    FILE *       f         = fopen(cmdPath, "r");

    if (f == NULL) {
        return;
    }
    char         line[512] = {0};

    if (fgets(line, sizeof(line), f) == NULL) {
        line[0] = '\0';
    }
    fclose(f);
    remove(cmdPath);

    size_t       len       = strlen(line);

    while ((len > 0) && ((line[len - 1] == '\n') || (line[len - 1] == '\r'))) {
        line[--len] = '\0';
    }
    char         cmd[32]   = {0};
    char *       space     = strchr(line, ' ');

    if (space != NULL) {
        size_t cmdLen = (size_t)(space - line);

        if (cmdLen >= sizeof(cmd)) {
            cmdLen = sizeof(cmd) - 1;
        }
        memcpy(cmd, line, cmdLen);
        cmd[cmdLen] = '\0';
        backdoor_dispatch(cmd, space + 1, win);
    } else {
        strncpy(cmd, line, sizeof(cmd) - 1);
        backdoor_dispatch(cmd, "", win);
    }
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
        // Cheap no-op access() check per iteration, and skipped entirely (returns immediately)
        // unless EMU_UTILITY_BACKDOOR is set — see the backdoor block's own header comment.
        backdoor_poll(win);

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
