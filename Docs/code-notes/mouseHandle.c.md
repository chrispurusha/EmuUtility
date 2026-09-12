# mouseHandle.c notes

The longer comments from `mouseHandle.c`, moved here 2026-09-12 so the code reads cleanly. The code points at each as `// notes §k`. Verbatim and in file order; each is titled by what it documents.

## 1. `get_global_gui_scaled_mouse_coord()`

Convert GLFW window-space (x,y) to logical canvas coordinates.
window_to_logical() moved into SynthLib (declared in inputState.h, implemented in
inputStateGlfw.c): it was character-identical here and in the other editor, and inlined in
the third — where it had lost the divide-by-zero guard both copies here kept. SynthLib owns
the window, so the shared one takes no window argument.

## 2. `gDialPrevX`

LOGICAL coordinates, not window pixels — the cursor handler is handed logical ones now. The
distinction does not change the drag: window_to_logical is a linear scale with no offset, so the
difference of two logical coordinates equals the converted difference of two window ones, which is
exactly what delta_to_logical() used to compute for this.

## 3. `end_dial_drag()`

Ends a dial drag. The real mouse release calls this, and so does recover_lost_dial_drag() when the
release never arrived — one place, so the two cannot drift apart.

No explicit glfwSetCursorPos() here: GLFW's cocoa backend already restores the cursor to wherever
it was when CURSOR_DISABLED was entered, as soon as we switch back to NORMAL, and two independent
warps in a row can land a pixel or two apart (same fix as SynthEdit's mouseHandle.c).

## 4. in `end_dial_drag()`

The dial's own press was dispatched through the click registry, so it captured (see
eClickPhase in clickRegion.h). This path consumes the release without reaching
dispatch_click_region(), so drop that capture explicitly — otherwise it stays armed and the
next release to land on empty space would be delivered to the dial handler.

## 5. `recover_lost_dial_drag()`

A drag whose release never arrived — button came up outside the window, focus lost mid-gesture —
used to leave gDialDragActive set FOREVER. That is worse than a stuck cursor: the MIDI thread
gates BOTH of its corrective refreshes on the drag being over (the settle, and the idle resync),
so a stuck flag disables both and the display stays wrong indefinitely, while a delta is requested
every 120 ms for a drag that ended long ago.

Called every frame from the render loop; a no-op unless the flag really is stuck. G2-Edit does the
same with recover_lost_cursor() — this app had no equivalent.

## 6. in `handle_mouse_button()`

A release that ends an active dial drag takes priority over other click
routing below (context menu, buttons) — the drag consumes the release
regardless of what's now under the cursor. Matches G2-Edit/mouseHandle.c.

No explicit glfwSetCursorPos() here (there used to be one, restoring
gDialStartX/Y) — GLFW's cocoa backend already restores the cursor to
wherever it was when CURSOR_DISABLED was entered as soon as we switch
back to NORMAL (see updateCursorMode() in cocoa_window.m). An extra
explicit warp on top of that was redundant, and — per the equivalent
fix in SynthEdit's mouseHandle.c — two independent warps in a row can
land a pixel or two off from each other. Harmless there in practice
since there's only the one dial on screen, but no reason to keep it.

## 7. in `handle_mouse_button()`

A MODAL POPUP GETS THE CLICK FIRST, AND UNTIL NOW GOT IT NEVER. This app renders and ticks
SynthLib's popup coordinator (synthlib_popups_render/_tick in graphics.c) but routed its
CLICKS past it entirely, so a modal popup could be raised and never dismissed — the renderer
switch's "will be used the next time" alert sat there with a dead OK button. Its Escape and
Return are dealt with the same way in handle_key().

Gated on synthlib_popups_modal_active() rather than dispatching unconditionally, which is
what G2-Edit and SynthEdit do: those two have handed their menu bar and context menu to the
coordinator as well, and this app has not. Gating means nothing about the existing path
changes while no modal popup is up, which is almost always.

handle_scroll() and handle_character() below close the same hole on the other two input
channels.

## 8. in `handle_mouse_button()`

Same click's mouse-down just opened/switched/closed this dropdown via
handle_menu_bar_click() above — landing back on the bar itself on mouse-up is not a
dropdown-item selection, so leave the state exactly as mouse-down left it. Must be
checked before handle_context_menu_click(): that call closes the menu itself whenever
coord doesn't land on any open item, which a bar click never does.

## 9. in `handle_mouse_button()`

The dial and every button register their rect at render time (see
emuGraphics.cpp) — that's the entire clickable surface below the menu
bar/context menu already handled above, so dispatch alone is
authoritative here; no legacy per-widget hit-test fallback needed.

## 10. in `handle_cursor_pos()`

Relative tracking — rotating the mouse around the dial centre turns
the encoder at the same angular rate, without pinning the indicator
to the raw mouse angle (there's no fixed "12 o'clock = value X" on a
real endless encoder, so snapping to the click position would jump).

## 11. `handle_character()`

Text input exists in this app ONLY for whatever modal popup is up — the file browser's filename
field is the one that takes any. There is no app-level text entry to fall through to, so this
forwards and stops. Registered as .character in graphics.c; before this the GLFW callback was
left unregistered altogether, on the reasoning that the app takes no text, which was true of the
app and not of the popups it can raise.
