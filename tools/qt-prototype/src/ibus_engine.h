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
 * Fire-and-forget: queues on the GLib thread and returns at once, so it can never
 * block the caller. Returns false only if OpenGlide isn't active. */
bool og_ibus_commit(const char *utf8);

/* Commit and WAIT until it has actually been applied. Only for callers on a
 * dedicated output thread — it blocks. Returns false if OpenGlide isn't active or
 * the wait timed out. Exists so uinput keystrokes (immediate) and IBus commits
 * (queued) stay in submission order; see the comment on the definition. */
bool og_ibus_commit_sync(const char *utf8, int timeout_ms);

/* Changes whenever the text target may have changed — focus_out, focus_in, or the
 * engine being disabled. Correction offsets computed against one generation must
 * never be applied under another: the mirror they index would still look valid
 * while pointing at a different document. Returns a counter, not a flag, because
 * returning to a field cannot be distinguished from arriving at a new one. */
int og_ibus_target_generation(void);

/* An input context currently holds focus (i.e. commits have somewhere to land). */
bool og_ibus_focused(void);

/* Raw capability bitmask last declared by the focused client, or -1 if none ever
 * did. IBUS_CAP_PREEDIT_TEXT is the interesting bit. */
int og_ibus_capabilities(void);

/* The focused client can display preedit text — i.e. the current word could stay
 * uncommitted and be edited without deleting anything. False also when no client
 * has reported capabilities at all. */
bool og_ibus_preedit_supported(void);

/* Where the focused application says its text cursor is, in screen pixels.
 *
 * Clients report this through the IBus `set_cursor_location` vfunc so an IME can
 * place a candidate popup next to the caret; OpenGlide uses it for the opposite
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
