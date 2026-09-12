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
// Notes: Docs/code-notes/graphics.c.md - "// notes §k" refers there.

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
#include "synthlibWindow.h"
#include "synthlibPopups.h"
#include "midiPortDialog.h"
#include "emuGraphics.h"
#include "mouseHandle.h"
#include "menus.h"
#include "midiComms.h"
#include "misc.h"
#include "graphics.h"
#include "contextMenu.h"
#include <strings.h>
#include "appMenuBar.h"
#include "synthlibHost.h"
#include "synthlibScale.h"
#include "synthlibPersistence.h"
#include "noteEntry.h"
#include "sampleDump.h"
#include "peptalk.h"      // peptalk_send_raw(), for the PEPTALK exploration command

static void setup_projection(GLFWwindow * win);

// ── GLFW callbacks ────────────────────────────────────────────────────────────

void resize_window(int w, int h) {
    glfwSetWindowSize((GLFWwindow *)synthlib_window(), w, h);
}

void reposition_window(int x, int y) {
    glfwSetWindowPos((GLFWwindow *)synthlib_window(), x, y);
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

static void on_window_refresh(void) {
    synthlib_request_redraw();
}

// Focus is a HANDLER rather than a bare shim: this app does real work on it — backing off the LCD
// polling while nobody is looking (LCD_UNFOCUSED_PROBE_MS) and releasing any held notes — so it
// keeps a function of its own, in SynthLib's normalised form.
static void on_window_focus(bool focused) {
    midi_set_window_focused(focused);

    if (!focused) {
        note_entry_all_notes_off();
    }
}

// notes §1

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

    render_backend_set_surface(fbW, fbH);
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

    // notes §2
    synthlib_popups_set_menu_bar(gAppMenuBar, app_menu_bar_rect);

    // SynthLib's MIDI Ports dialogue, which the coordinator does not carry itself because it needs
    // CoreMIDI - see midiPortDialog.h. Opened from Device > MIDI Ports.
    synthlib_popups_register(midi_port_dialog_popup(), 1);

    synthlib_window_create(&(tSynthLibWindowConfig){
        .title        = title,
        .targetWidth  = TARGET_FRAME_BUFF_WIDTH,
        .targetHeight = TARGET_FRAME_BUFF_HEIGHT,
        .dialMode     = eDialModeVertical,
        .theme        = (tSynthLibTheme){
            .topBarHeight   = TOP_BAR_HEIGHT,
            .orange1        = (tRgb)RGB_ORANGE_1,
            .orange2        = (tRgb)RGB_ORANGE_2,
            .greenOn        = (tRgb)RGB_GREEN_ON,
            .backgroundGrey = (tRgb)RGB_BACKGROUND_GREY,
        },
        .mouseCoord   = get_global_gui_scaled_mouse_coord,
        .handlers     = &(const tSynthLibInputHandlers){
            .mouseButton   = handle_mouse_button,
            .cursorPos     = handle_cursor_pos,
            .key           = handle_key,
            .character     = handle_character,
            .scroll        = handle_scroll,
            .windowFocus   = on_window_focus,
            .windowRefresh = on_window_refresh,
        },
    }, NULL);

    init_font();        // TODO - G2 edit could benefit from this if we're loading multiple fonts
    init_lcd_texture();

    register_midi_wake_cb(wake_glfw);   // TODO - this doesn't belong in here
}

// ── Render frame ──────────────────────────────────────────────────────────────

static void render_frame(GLFWwindow * win) {
    setup_projection(win);

    render_backend_clear((tRgb){0.2, 0.2, 0.2});

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
    // notes §3
    render_menu_bar(gAppMenuBar, app_menu_bar_rect());
    synthlib_popups_render();

    // Submits the frame's one vertex array and puts it on screen. This was a render_backend_flush()
    // followed by glfwSwapBuffers() — the last GLFW call in this loop, and one that cannot exist
    // under Metal, where the window is created with no context to swap. See utilsGraphics.h.
    render_present();
}

// notes §4
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

// Does `wanted` appear anywhere in `label`, case-insensitively? Matched anywhere rather than only
// at the front because menu labels can carry a leading marker ("* " for a current selection), and a
// leading-substring match could then never name the thing being selected.
static bool backdoor_label_contains(const char * label, const char * wanted) {
    size_t wantedLength = strlen(wanted);
    size_t labelLength  = strlen(label);
    size_t at           = 0;

    if ((wantedLength == 0) || (wantedLength > labelLength)) {
        return false;
    }

    for (at = 0; at <= (labelLength - wantedLength); at++) {
        if (strncasecmp(label + at, wanted, wantedLength) == 0) {
            return true;
        }
    }

    return false;
}

// notes §5
static void backdoor_menu(const char * arg) {
    char        part[3][64] = {0};
    uint32_t    partCount   = 0;
    uint32_t    b           = 0;
    tMenuItem * items       = NULL;

    {
        const char * p = arg;

        while ((partCount < 3) && (*p != '\0')) {
            const char * slash  = strchr(p, '/');
            size_t       length = (slash != NULL) ? (size_t)(slash - p) : strlen(p);

            while ((length > 0) && (*p == ' ')) {
                p++;
                length--;
            }

            while ((length > 0) && (p[length - 1] == ' ')) {
                length--;
            }

            if (length >= sizeof(part[0])) {
                length = sizeof(part[0]) - 1;
            }
            memcpy(part[partCount], p, length);
            part[partCount][length] = '\0';
            partCount++;

            if (slash == NULL) {
                break;
            }
            p                       = slash + 1;
        }
    }

    if (partCount == 0) {
        backdoor_write_result("ERROR: expected 'MENU <bar>[/<item>[/<subitem>]]'\n");
        return;
    }

    for (b = 0; gAppMenuBar[b].label != NULL; b++) {
        if (backdoor_label_contains(gAppMenuBar[b].label, part[0]) == true) {
            break;
        }
    }

    if (gAppMenuBar[b].label == NULL) {
        backdoor_write_result("ERROR: no such menu\n");
        return;
    }
    // Populates gContextMenu with the items that menu would show RIGHT NOW, which is what makes
    // state-dependent labels ("Use Metal Renderer") matchable at all.
    gAppMenuBar[b].open((tCoord){0.0, 0.0});
    items = (gContextMenu.depth > 0) ? gContextMenu.frame[0].items : NULL;

    {
        uint32_t level = 1;

        while ((level < partCount) && (items != NULL)) {
            uint32_t i     = 0;
            bool     found = false;

            for (i = 0; items[i].label != NULL; i++) {
                if (backdoor_label_contains(items[i].label, part[level]) == false) {
                    continue;
                }
                found = true;

                if (level == (partCount - 1)) {
                    if (items[i].subMenu != NULL) {
                        items = items[i].subMenu;
                        break;
                    }
                    {
                        void (*action)(int index) = items[i].action;

                        // action() callbacks read gContextMenu.items[index].param, so point that at
                        // the array the item lives in — the same thing a real click does.
                        gContextMenu.items = items;

                        if (action == NULL) {
                            close_context_menu();
                            backdoor_write_result("ERROR: item is disabled\n");
                            return;
                        }
                        action((int)i);
                        close_context_menu();
                        synthlib_request_redraw();
                        backdoor_write_result("OK\n");
                        return;
                    }
                }

                if (items[i].subMenu == NULL) {
                    close_context_menu();
                    backdoor_write_result("ERROR: that item has no submenu\n");
                    return;
                }
                items = items[i].subMenu;
                break;
            }

            if (found == false) {
                close_context_menu();
                backdoor_write_result("ERROR: no such item\n");
                return;
            }
            level++;
        }
    }

    // Nothing left to click: list what the level we reached contains.
    {
        char     list[2048] = {0};
        size_t   used       = 0;
        uint32_t i          = 0;

        used += (size_t)snprintf(list + used, sizeof(list) - used, "OK\n");

        for (i = 0; (items != NULL) && (items[i].label != NULL) && (used < sizeof(list)); i++) {
            used += (size_t)snprintf(list + used, sizeof(list) - used, "%s%s\n",
                                     items[i].label, (items[i].subMenu != NULL) ? " >" : "");
        }

        close_context_menu();
        backdoor_write_result(list);
    }
}

// Reads the framebuffer as it stands. Deliberately does NOT render first — see the SCREENGRAB entry
// in the command list above.
static void backdoor_capture(const char * path) {
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

    // Tight row packing, and the sheared-PNG bug behind it, are inside the backend call now.
    if (!render_backend_read_pixels_rgb(0, 0, w, h, pixels)) {
        free(pixels);
        backdoor_write_result("ERROR: frame read-back failed\n");
        return;
    }
    stbi_flip_vertically_on_write(1);

    int       ok     = stbi_write_png(path, w, h, 3, pixels, w * 3);

    free(pixels);
    backdoor_write_result(ok ? "OK\n" : "ERROR: stbi_write_png failed\n");
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

    // Tight row packing, and the sheared-PNG bug behind it, are inside the backend call now.
    if (!render_backend_read_pixels_rgb(0, 0, w, h, pixels)) {
        free(pixels);
        backdoor_write_result("ERROR: frame read-back failed\n");
        return;
    }
    stbi_flip_vertically_on_write(1); // read-back origin is bottom-left; PNGs are top-down

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
    size_t   used      = 0;

    used += (size_t)snprintf(out + used, outMax - used,
                             "OK\nsession=%s connected=%s deviceId=0x%02X family=%u member=%u\n",
                             gSessionOpen ? "open" : "closed",
                             gDevice.connected ? "yes" : "no",
                             gDevice.id, (unsigned)gDevice.family, (unsigned)gDevice.member);

    uint32_t rotaryIn  = 0;
    uint32_t rotaryOut = 0;

    midi_rotary_counts(&rotaryIn, &rotaryOut);
    used += (size_t)snprintf(out + used, outMax - used, "rotaryTicksIn=%u rotaryMessagesOut=%u\n",
                             (unsigned)rotaryIn, (unsigned)rotaryOut);

    // notes §6
    used += (size_t)snprintf(out + used, outMax - used, "pid=%d\n", (int)getpid());

    used += (size_t)snprintf(out + used, outMax - used, "pressSettleMs=%.0f\n", midi_press_settle_ms());

    used += (size_t)snprintf(out + used, outMax - used, "focused=%s\n",
                             midi_window_focused() ? "yes" : "no");

    used += (size_t)snprintf(out + used, outMax - used, "lcdQuiet=%s\n",
                             midi_lcd_is_quiet() ? "yes" : "no");

    used += (size_t)snprintf(out + used, outMax - used, "noteEntryFirstNote=%u\n",
                             (unsigned)note_entry_first_note());

    // The render->framebuffer factor, so a scripted click can turn any rectangle reported below into
    // a real screen coordinate: framebuffer px = render units * guiScale, screen points = that / the
    // display's backing scale, offset by the window's content origin.
    used += (size_t)snprintf(out + used, outMax - used, "guiScale=%.4f render=%dx%d\n",
                             gGlobalGuiScale, get_render_width(), get_render_height());

    tRectangle dial = emu_dial_rect();

    used += (size_t)snprintf(out + used, outMax - used, "dial rect=%.1f,%.1f %.1fx%.1f\n",
                             dial.coord.x, dial.coord.y, dial.size.w, dial.size.h);

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
    } else if (strcmp(cmd, "MENU") == 0) {
        backdoor_menu(arg);
    } else if (strcmp(cmd, "SCREENGRAB") == 0) {
        backdoor_capture(arg);
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
            handle_key(glfwKey, 0, GLFW_PRESS, 0);
        }

        if (up) {
            handle_key(glfwKey, 0, GLFW_RELEASE, 0);
        }
        synthlib_request_redraw();
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "BURST") == 0) {
        char       name[32] = {0};
        int        count    = 0;
        int        gapMs    = 0;
        tButtonKey key      = 0;

        if (sscanf(arg, "%31s %d %d", name, &count, &gapMs) != 3) {
            backdoor_write_result("ERROR: expected 'BURST <label|code> <count> <gapMs>'\n");
            return;
        }

        if (!emu_button_lookup(name, &key)) {
            backdoor_write_result("ERROR: no such button\n");
            return;
        }

        if ((count < 1) || (count > 200) || (gapMs < 0) || (gapMs > 2000)) {
            backdoor_write_result("ERROR: count 1-200, gapMs 0-2000\n");
            return;
        }

        for (int i = 0; i < count; i++) {
            emu_button_press(key, true);
            usleep((useconds_t)gapMs * 1000);
            emu_button_press(key, false);
            usleep((useconds_t)gapMs * 1000);
        }

        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "SPIN") == 0) {
        int steps = 0;
        int gapMs = 0;

        if (sscanf(arg, "%d %d", &steps, &gapMs) != 2) {
            backdoor_write_result("ERROR: expected 'SPIN <steps> <gapMs>'\n");
            return;
        }

        if ((steps == 0) || (steps > 400) || (steps < -400) || (gapMs < 0) || (gapMs > 500)) {
            backdoor_write_result("ERROR: steps -400..400 (non-zero), gapMs 0-500\n");
            return;
        }
        int dir   = (steps > 0) ? 1 : -1;
        int count = (steps > 0) ? steps : -steps;

        for (int i = 0; i < count; i++) {
            dial_nudge(dir);
            usleep((useconds_t)gapMs * 1000);
        }

        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "SAMPLEINFO") == 0) {
        tSampleDump dump     = {0};
        char        why[256] = {0};
        char        text[768];

        if (!sample_dump_load_wav(arg, 0, &dump, why, sizeof(why))) {
            snprintf(text, sizeof(text), "REJECTED: %s\n", why);
            backdoor_write_result(text);
            return;
        }
        double      secs     = sample_dump_estimate_seconds(&dump);

        snprintf(text, sizeof(text),
                 "OK\nname=%s\nsource=%u-bit %s @ %u Hz\nsending=16-bit mono, %u samples (%.2fs audio)\n"
                 "loop=%s%s\npackets=%u\nestimatedTransfer=%.0fs (%.1f min)\n",
                 dump.sourceName, dump.srcBits, (dump.srcChannels == 2) ? "stereo" : "mono",
                 dump.sampleRate, dump.frameCount, (double)dump.frameCount / (double)dump.sampleRate,
                 dump.hasLoop ? "yes" : "none", dump.loopAlternating ? " (alternating)" : "",
                 sample_dump_packet_count(&dump), secs, secs / 60.0);
        backdoor_write_result(text);
        sample_dump_free(&dump);
    } else if (strcmp(cmd, "SENDSAMPLE") == 0) {
        char         path[400] = {0};
        int          number    = -1;
        const char * lastSpace = strrchr(arg, ' ');

        if ((lastSpace == NULL) || (sscanf(lastSpace + 1, "%d", &number) != 1)) {
            backdoor_write_result("ERROR: expected 'SENDSAMPLE <path> <sampleNumber>'\n");
            return;
        }
        size_t       pathLen   = (size_t)(lastSpace - arg);

        if (pathLen >= sizeof(path)) {
            backdoor_write_result("ERROR: path too long\n");
            return;
        }
        memcpy(path, arg, pathLen);

        if ((number < 0) || (number > (int)SDS_MAX_SAMPLE_NUMBER)) {
            backdoor_write_result("ERROR: sample number out of range\n");
            return;
        }
        tSampleDump  dump      = {0};
        char         why[256]  = {0};
        char         text[512];

        if (!sample_dump_load_wav(path, 0, &dump, why, sizeof(why))) {
            snprintf(text, sizeof(text), "ERROR: %s\n", why);
            backdoor_write_result(text);
            return;
        }
        snprintf(text, sizeof(text), "OK\nsending %u samples as #%d, %u packets, about %.0fs\n",
                 (unsigned)dump.frameCount, number, (unsigned)sample_dump_packet_count(&dump),
                 sample_dump_estimate_seconds(&dump));
        midi_post_sds_start(&dump, (uint16_t)number, gDevice.id);   // ownership moves to the MIDI thread
        backdoor_write_result(text);
    } else if (strcmp(cmd, "SDSDRYRUN") == 0) {
        char        inPath[400]  = {0};
        char        outPath[400] = {0};

        if (sscanf(arg, "%399s %399s", inPath, outPath) != 2) {
            backdoor_write_result("ERROR: expected 'SDSDRYRUN <wav> <outFile>'\n");
            return;
        }
        tSampleDump dump         = {0};
        char        why[256]     = {0};
        char        text[512];

        if (!sample_dump_load_wav(inPath, 0, &dump, why, sizeof(why))) {
            snprintf(text, sizeof(text), "ERROR: %s\n", why);
            backdoor_write_result(text);
            return;
        }
        FILE *      out          = fopen(outPath, "wb");

        if (out == NULL) {
            backdoor_write_result("ERROR: cannot write the output file\n");
            sample_dump_free(&dump);
            return;
        }
        uint8_t     frame[128];
        uint32_t    len          = sample_dump_build_header(&dump, 0, 1, frame);
        uint32_t    total        = len;

        fwrite(frame, 1, len, out);

        for (uint32_t p = 0; ; p++) {
            len    = sample_dump_build_packet(&dump, 0, p, frame);

            if (len == 0) {
                break;
            }
            fwrite(frame, 1, len, out);
            total += len;
        }

        fclose(out);
        snprintf(text, sizeof(text), "OK\nbytes=%u packets=%u words=%u rate=%u\n",
                 (unsigned)total, (unsigned)sample_dump_packet_count(&dump),
                 (unsigned)dump.frameCount, (unsigned)dump.sampleRate);
        backdoor_write_result(text);
        sample_dump_free(&dump);
    } else if (strcmp(cmd, "GETSAMPLE") == 0) {
        int  number       = -1;
        char outPath[400] = {0};

        if (sscanf(arg, "%d %399s", &number, outPath) != 2) {
            backdoor_write_result("ERROR: expected 'GETSAMPLE <sampleNumber> <out.wav>'\n");
            return;
        }

        if ((number < 0) || (number > (int)SDS_MAX_SAMPLE_NUMBER)) {
            backdoor_write_result("ERROR: sample number out of range\n");
            return;
        }
        midi_post_sds_request((uint16_t)number, outPath);
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "SDSRX") == 0) {
        uint32_t got        = 0;
        uint32_t total      = 0;
        char     status[96] = {0};
        bool     running    = midi_sds_rx_progress(&got, &total, status, sizeof(status));
        char     text[256];

        snprintf(text, sizeof(text), "OK\nreceiving=%s words=%u/%u status=%s\n",
                 running ? "yes" : "no", (unsigned)got, (unsigned)total, status);
        backdoor_write_result(text);
    } else if (strcmp(cmd, "SDSPROGRESS") == 0) {
        uint32_t sent   = 0;
        uint32_t total  = 0;
        bool     closed = false;
        bool     active = midi_sds_progress(&sent, &total, &closed);
        char     text[256];

        snprintf(text, sizeof(text), "OK\nactive=%s packets=%u/%u loop=%s\n",
                 active ? "yes" : "no", (unsigned)sent, (unsigned)total,
                 closed ? "closed" : "open");
        backdoor_write_result(text);
    } else if (strcmp(cmd, "SDSCANCEL") == 0) {
        midi_post_sds_cancel();
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "PRESSDELAY") == 0) {
        double ms = -1.0;

        if ((sscanf(arg, "%lf", &ms) != 1) || (ms < 0.0) || (ms > 1000.0)) {
            backdoor_write_result("ERROR: expected 'PRESSDELAY <0-1000 ms>'\n");
            return;
        }
        midi_set_press_settle_ms(ms);
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "PEPTALK") == 0) {
        unsigned     type     = 0;
        uint8_t      data[32] = {0};
        uint32_t     len      = 0;
        const char * at       = arg;
        char *       end      = NULL;

        type = (unsigned)strtoul(at, &end, 16);

        if ((end == at) || (type > 0x7F)) {
            backdoor_write_result("ERROR: expected 'PEPTALK <type hex> [byte hex ...]'\n");
            return;
        }
        at   = end;

        while ((len < sizeof(data))) {
            unsigned long b = strtoul(at, &end, 16);

            if (end == at) {
                break;
            }
            data[len++] = (uint8_t)b;
            at          = end;
        }
        peptalk_send_raw((uint8_t)type, (len > 0) ? data : NULL, len);
        backdoor_write_result("OK\n");
    } else if (strcmp(cmd, "REFRESH") == 0) {
        midi_post_lcd_refresh(true);
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
        // notes §7
        synthlib_popups_tick();

        bool reDraw = synthlib_consume_redraw();

        if (reDraw) {
            render_frame(win);
        }
        // notes §8
        recover_lost_dial_drag(win);

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
