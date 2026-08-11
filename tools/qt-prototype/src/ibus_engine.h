/* ibus_engine.h — persistent OpenGlide IBus engine host (C API).
 *
 * Hosts the "openglide" engine in-process and commits text via its input
 * context with ibus_engine_commit_text (UTF-8, layout-independent; ADR-0002
 * primary path). Self-activates as the global engine on startup (in-process —
 * external SetGlobalEngine can't resolve a runtime engine) and forwards physical
 * key events (pass-through) so typing still works while it's active; restores
 * the previous engine on shutdown. uinput is the fallback if IBus isn't ready. */
#pragma once
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the IBus thread: ibus_init + connect + register "openglide" + run a
 * GLib main loop so ibus-daemon can CreateEngine back into us. Call once. */
bool og_ibus_start(void);

/* Connected to ibus-daemon and the engine is registered. */
bool og_ibus_connected(void);

/* OpenGlide is the active IME with a focused input context (a commit will land
 * in the focused field via IBus). */
bool og_ibus_active(void);

/* Commit NUL-terminated UTF-8 to the focused field via ibus_engine_commit_text.
 * Synchronous: returns true once committed; false if OpenGlide isn't active
 * (caller should fall back to uinput). */
bool og_ibus_commit(const char *utf8);

/* Delete n chars from the focused field (forward KEY_BACKSPACE via the engine).
 * Same path as og_ibus_commit. Returns false if OpenGlide isn't active. */
bool og_ibus_backspace(int n);

/* Where the focused application says its text cursor is, in screen pixels.
 *
 * Clients report this through the IBus `set_cursor_location` vfunc so an IME can
 * place its candidate popup next to the caret; OpenGlide uses it for the opposite
 * reason — to get its window OUT of the way of the text being typed (spec §17 is
 * about focus, this is about occlusion).
 *
 * Returns false if no client has ever reported one, which is the normal case for
 * apps that don't bother. Coordinates are root-window coordinates; treat a rect
 * of zero width AND height as "position only". */
bool og_ibus_cursor_rect(int *x, int *y, int *w, int *h);

/* How many location reports have arrived. 0 means the focused toolkit never
 * reports — the caller should not wait for it. */
unsigned og_ibus_cursor_reports(void);

/* Restore the previous global engine. Call at shutdown. */
void og_ibus_shutdown(void);

#ifdef __cplusplus
}
#endif
