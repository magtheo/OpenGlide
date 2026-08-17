/* ibus_engine.c — persistent OpenGlide IBus engine host.
 *
 * Evolved from ibus-engine-probe/main.c (the validated ADR-0002 gate). The app
 * hosts the engine itself: on startup it registers "openglide" + self-activates
 * it as the global engine (in-process set_global_engine_async — the one path the
 * probe proved works; external `ibus engine openglide` can't resolve a runtime-
 * registered engine). While OpenGlide is active it FORWARDS physical key events
 * (pass-through) so typing still works, and glide decodes commit via
 * ibus_engine_commit_text (UTF-8, layout-independent). On shutdown the previous
 * engine is restored. uinput is the layout-bound fallback if IBus isn't ready.
 *
 * GLib runs in its own thread (its own GMainContext); Qt never touches GLib
 * directly. Cross-thread ops marshal via g_main_context_invoke (synchronous). */
#include "ibus_engine.h"

#include <ibus.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <glib-object.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- a pass-through IBusEngine: forwards keys + reports enable/disable ---- */
#define OG_TYPE_ENGINE (og_engine_get_type())
typedef struct _OgEngine      OgEngine;
typedef struct _OgEngineClass OgEngineClass;
struct _OgEngine      { IBusEngine parent; };
struct _OgEngineClass { IBusEngineClass parent_class; };

static GType og_engine_get_type(void);
G_DEFINE_TYPE(OgEngine, og_engine, IBUS_TYPE_ENGINE)

static GMainContext        *g_ctx = NULL;
static GMainLoop           *g_loop = NULL;
static IBusBus             *g_bus = NULL;
static gchar               *g_prev = NULL;          /* saved global engine name */
static volatile IBusEngine *g_engine = NULL;        /* non-NULL => active + has a context */
static volatile gint        g_connected = 0;
static GThread           *g_ibus_thread_id = NULL;   /* for inline dispatch when already on the IBus thread */
static volatile gint        g_target_gen = 0;        /* see og_ibus_target_generation */
static volatile gint        g_focused = 0;
/* Self-test (OPENGLIDE_IBUS_TEST=1) run from a SEPARATE thread — exercises the
 * cross-thread commit path (g_main_context_invoke) that the inline call at enable
 * does NOT, and which is what real glide commits use. Backspace is no longer
 * tested here: it's a uinput key event now (delete_surrounding_text crashed
 * gnome-shell), so it lives in the injector, not the IBus path. */
static gpointer test_thread(gpointer p) {
    (void)p;
    g_usleep(100000);   /* 100 ms: let the focused sink settle */
    bool c = og_ibus_commit("abcde");
    g_usleep(50000);
    bool r = og_ibus_commit("xy");   /* second commit must land AFTER the first */
    fprintf(stderr, "[openglide-ibus] TEST commit=%d recommit=%d (expect 'abcdexy')\n", c, r);
    return NULL;
}
static void og_enable(IBusEngine *engine) {
    g_atomic_pointer_set(&g_engine, engine);        /* now bound to the focused context */
    fprintf(stderr, "[openglide-ibus] enabled — commits route via IBus\n");
    if (getenv("OPENGLIDE_IBUS_TEST"))              /* self-test from a separate thread */
        g_thread_new("og-test", test_thread, NULL);
}
static void og_disable(IBusEngine *engine) {
    (void)engine;
    g_atomic_pointer_set(&g_engine, NULL);
    g_atomic_int_inc(&g_target_gen);    /* the field we were editing is gone */
    g_atomic_int_set(&g_focused, 0);
    fprintf(stderr, "[openglide-ibus] disabled\n");
}
/* ---- key routing policy -------------------------------------------------
 *
 * The engine must not swallow physical keys while it is the active IME. Two
 * ways to achieve that, and which one is correct is an OPEN QUESTION:
 *
 *   forward  — ibus_engine_forward_key_event + return TRUE. Re-injects the key
 *              as a SYNTHETIC event into the focused input context. Typing works,
 *              but the compositor never sees the real key, so any globally
 *              grabbed shortcut dies: Super+1 typed "1" instead of opening dock
 *              slot 1. Alt+Tab, Ctrl+Alt+arrows, Print Screen are the same class.
 *   pass     — return FALSE. "Not mine" — IBus delivers it down the normal path,
 *              so the compositor and the client both see the real event.
 *
 * `pass` (default) returns FALSE for every key — "not mine" — so IBus delivers it
 * down the normal path and BOTH the compositor and the client see the real event.
 * Verified on hardware: ordinary typing, Super+1 (dock) and Alt+Tab all work, so
 * this fixes every compositor shortcut at once (Super+1 used to type "1" under the
 * old forward path, which re-injects a synthetic event the compositor never sees).
 * `forward` and `auto` stay as opt-in fallbacks in case pass misbehaves elsewhere.
 *
 *   OPENGLIDE_KEY_ROUTING=pass      -> return FALSE for everything (DEFAULT)
 *   OPENGLIDE_KEY_ROUTING=forward   -> forward everything (old behaviour, fallback)
 *   OPENGLIDE_KEY_ROUTING=auto      -> pass Super, forward the rest (old default) */
enum { OG_ROUTE_AUTO = 0, OG_ROUTE_PASS, OG_ROUTE_FORWARD };
static int g_key_routing = -1;      /* resolved once, on first key */
static int g_log_content = -1;      /* content logging opt-in (ADR-0004) */

/* ADR-0004: key names ARE user content — this is a keystroke log. It must stay
 * behind a deliberate, announced, non-persistent opt-in, so it is resolved once
 * and shouts when it is on. Writes to stderr rather than a file: visible to the
 * user, and gone when the session ends. */
static gboolean og_content_logging(void) {
    if (g_log_content < 0) {
        const char *v = getenv("OPENGLIDE_LOG_CONTENT");
        if (!v) v = getenv("OPENGLIDE_KEY_DEBUG");   /* accepted alias */
        g_log_content = (v && *v && strcmp(v, "0") != 0) ? 1 : 0;
        if (g_log_content)
            fprintf(stderr,
                "\n*** OPENGLIDE CONTENT LOGGING IS ON ***\n"
                "    Every key you press will be printed to stderr, including\n"
                "    passwords. This session only. Unset OPENGLIDE_LOG_CONTENT\n"
                "    (or OPENGLIDE_KEY_DEBUG) and restart to turn it off.\n\n");
    }
    return g_log_content == 1;
}

static int og_key_routing(void) {
    if (g_key_routing < 0) {
        const char *v = getenv("OPENGLIDE_KEY_ROUTING");
        if (v && strcmp(v, "pass") == 0)         g_key_routing = OG_ROUTE_PASS;
        else if (v && strcmp(v, "forward") == 0) g_key_routing = OG_ROUTE_FORWARD;
        else                                     g_key_routing = OG_ROUTE_PASS;  /* default — verified on hardware */
        if (g_key_routing != OG_ROUTE_PASS)
            fprintf(stderr, "[openglide-ibus] key routing = %s (non-default)\n", v);
    }
    return g_key_routing;
}

static gboolean og_process_key_event(IBusEngine *engine, guint keyval, guint keycode, guint state) {
    const int route = og_key_routing();
    const gboolean super = (state & IBUS_MOD4_MASK) ||
        keyval == IBUS_KEY_Super_L || keyval == IBUS_KEY_Super_R;
    /* pass = never claim; forward = always claim; auto = pass only Super combos */
    const gboolean pass = (route == OG_ROUTE_PASS) ||
                          (route == OG_ROUTE_AUTO && super);
    /* diagnostic: is the uinput/physical BackSpace even reaching the engine? */
    if (keyval == IBUS_KEY_BackSpace && !(state & IBUS_RELEASE_MASK))
        fprintf(stderr, "[bs] engine received BackSpace press -> %s\n", pass ? "pass" : "forward");
    if (og_content_logging()) {
        const char *n = ibus_keyval_name(keyval);
        fprintf(stderr, "[key] %s state=0x%X -> %s\n", n ? n : "?", state,
                pass ? "pass (compositor/client)" : "forward (synthetic)");
    }
    if (pass)
        return FALSE;                          /* let it continue down the normal path */
    ibus_engine_forward_key_event(engine, keyval, keycode, state);
    return TRUE;
}
/* ---- target identity ------------------------------------------------------
 * Every correction the UI offers is "delete N characters, then retype", computed
 * from offsets into a mirror of the focused field. If focus moves to a DIFFERENT
 * field those offsets still look valid — the mirror did not change — but they now
 * address the wrong document, so clicking a stale suggestion would silently edit
 * whatever the user switched to.
 *
 * The input context tells us: focus_out when the field goes away, focus_in when a
 * new one arrives. Publish a counter that changes on either, so the UI can stamp
 * each history entry with the target it belongs to and refuse to act on entries
 * from a previous one. Deliberately a generation counter, not a boolean: coming
 * BACK to a field is a new generation too, because we cannot prove it is the same
 * field. Fail safe, not fail convenient. */

static void og_focus_in(IBusEngine *engine) {
    (void)engine;
    g_atomic_int_inc(&g_target_gen);
    g_atomic_int_set(&g_focused, 1);
}
static void og_focus_out(IBusEngine *engine) {
    (void)engine;
    g_atomic_int_inc(&g_target_gen);
    g_atomic_int_set(&g_focused, 0);
}

int  og_ibus_target_generation(void) { return (int)g_atomic_int_get(&g_target_gen); }
bool og_ibus_focused(void) { return g_atomic_int_get(&g_focused) != 0; }

/* ---- client capabilities -------------------------------------------------
 * The focused client declares what it can render. IBUS_CAP_PREEDIT_TEXT is the
 * one that matters here: preedit (keeping the current word UNCOMMITTED so editing
 * it needs no deletion) is only viable in clients that can display it, and
 * terminals and some Electron apps cannot. Deletion is currently broken on GNOME
 * — delete_surrounding_text aborts the shell, forwarded BackSpace is ignored, and
 * uinput targets the wrong context — so preedit is the obvious way out, but only
 * where this bit is set. Probe before building on it. */
static volatile gint g_caps = -1;   /* -1 = never reported */

static void og_set_capabilities(IBusEngine *engine, guint caps) {
    (void)engine;
    g_atomic_int_set(&g_caps, (gint)caps);
}

int og_ibus_capabilities(void) { return (int)g_atomic_int_get(&g_caps); }

bool og_ibus_preedit_supported(void) {
    const gint c = g_atomic_int_get(&g_caps);
    return c >= 0 && (((guint)c) & IBUS_CAP_PREEDIT_TEXT) != 0;
}

/* ---- caret location (spec: keep the keyboard off the text) ----------------
 * set_cursor_location arrives on the GLib thread; QML reads it on the Qt thread,
 * so the rect is published under a mutex. Guarded by a report counter so callers
 * can tell "no client ever reported" from "reported at 0,0". */
static GMutex   g_caret_mu;
static int      g_caret[4] = {0, 0, 0, 0};
static unsigned g_caret_n  = 0;

static void og_set_cursor_location(IBusEngine *engine, gint x, gint y, gint w, gint h) {
    (void)engine;
    g_mutex_lock(&g_caret_mu);
    g_caret[0] = x; g_caret[1] = y; g_caret[2] = w; g_caret[3] = h;
    g_caret_n++;
    g_mutex_unlock(&g_caret_mu);
}

bool og_ibus_cursor_rect(int *x, int *y, int *w, int *h) {
    bool have;
    g_mutex_lock(&g_caret_mu);
    have = g_caret_n > 0;
    if (have) {
        if (x) *x = g_caret[0];
        if (y) *y = g_caret[1];
        if (w) *w = g_caret[2];
        if (h) *h = g_caret[3];
    }
    g_mutex_unlock(&g_caret_mu);
    return have;
}

unsigned og_ibus_cursor_reports(void) {
    unsigned n;
    g_mutex_lock(&g_caret_mu);
    n = g_caret_n;
    g_mutex_unlock(&g_caret_mu);
    return n;
}

static void og_engine_init(OgEngine *self) { (void)self; }
static void og_engine_class_init(OgEngineClass *klass) {
    IBUS_ENGINE_CLASS(klass)->enable            = og_enable;
    IBUS_ENGINE_CLASS(klass)->disable           = og_disable;
    IBUS_ENGINE_CLASS(klass)->process_key_event = og_process_key_event;
    IBUS_ENGINE_CLASS(klass)->set_cursor_location = og_set_cursor_location;
    IBUS_ENGINE_CLASS(klass)->set_capabilities    = og_set_capabilities;
    IBUS_ENGINE_CLASS(klass)->focus_in            = og_focus_in;
    IBUS_ENGINE_CLASS(klass)->focus_out           = og_focus_out;
}

static gpointer ibus_thread(gpointer arg) {
    (void)arg;
    g_ibus_thread_id = g_thread_self();   /* mark this as the IBus/GLib thread */
    g_main_context_push_thread_default(g_ctx);   /* bind the bus to this loop */

    ibus_init();
    g_bus = ibus_bus_new();
    /* ibus_bus_new() connects ASYNCHRONOUSLY — the socket only completes while a
     * GLib main context iterates, and the loop below starts only after this
     * check. is_connected() immediately after bus_new() is therefore FALSE by
     * construction; whatever box printed "connected" won a race. Pump this
     * thread's context until the async connect lands, bounded (~2 s) so a dead
     * daemon still falls through to uinput quickly. */
    for (int i = 0; i < 200 && !ibus_bus_is_connected(g_bus); i++) {
        if (!g_main_context_iteration(g_ctx, FALSE)) g_usleep(10000);
    }
    if (!ibus_bus_is_connected(g_bus)) {
        fprintf(stderr, "[openglide-ibus] not connected to ibus-daemon (uinput only)\n");
        return NULL;
    }
    g_atomic_int_set(&g_connected, 1);

    IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(g_bus));
    ibus_factory_add_engine(factory, "openglide", OG_TYPE_ENGINE);
    ibus_bus_request_name(g_bus, "org.freedesktop.IBus.OpenGlide", 0);

    /* Every string field populated — register_component's GVariant serialization
     * rejects NULLs (learned from the probe). command_line is the .engine field. */
    IBusComponent *comp = ibus_component_new_varargs(
        "name", "org.freedesktop.IBus.OpenGlide",
        "description", "OpenGlide glide keyboard",
        "version", "0.1", "license", "GPL-3.0",
        "author", "OpenGlide", "homepage", "https://openglide.example",
        "command_line", "openglide-qt", "textdomain", "ibus", NULL);
    IBusEngineDesc *desc = ibus_engine_desc_new_varargs(
        "name", "openglide", "longname", "OpenGlide",
        "description", "Glide keyboard (pass-through)", "language", "en",
        "license", "GPL-3.0", "author", "OpenGlide", "icon", "",
        "layout", "default", "layout_variant", "", "layout_option", "",
        "rank", "0", "symbol", "", "setup", "", "hotkeys", "",
        "version", "0.1", "textdomain", "ibus", NULL);
    ibus_component_add_engine(comp, desc);
    gboolean rc = ibus_bus_register_component(g_bus, comp);
    fprintf(stderr, "[openglide-ibus] connected, registered 'openglide' (rc=%d)\n", rc);

    /* Self-activate (in-process async — the path the probe proved works; external
     * SetGlobalEngine can't resolve a runtime engine). Save the user's engine first. */
    IBusEngineDesc *cur = ibus_bus_get_global_engine(g_bus);
    if (cur) { g_free(g_prev); g_prev = g_strdup(ibus_engine_desc_get_name(cur)); }
    ibus_bus_set_global_engine_async(g_bus, "openglide", -1, NULL, NULL, NULL);
    fprintf(stderr, "[openglide-ibus] self-activating openglide (was: %s)\n",
            g_prev ? g_prev : "(none)");

    g_loop = g_main_loop_new(g_ctx, FALSE);
    g_main_loop_run(g_loop);
    return NULL;
}

/* KDE/kwin launches ibus-daemon through ibus-wayland WITH a DISPLAY set, so the
 * daemon writes the X-style address file (<machine-id>-unix-0). Clients living in
 * a Wayland session (WAYLAND_DISPLAY set) look for the wayland-style file
 * instead — stale or absent here — and ibus_get_address() returns NULL, so no
 * bus, ever. Resolve it ourselves: scan the bus dir, keep the newest file whose
 * IBUS_DAEMON_PID is still alive, and pin IBUS_ADDRESS (documented to override
 * file resolution). Verified against the live daemon on Plasma 6, 2026-08-17. */
static void og_resolve_ibus_address(void) {
    if (g_getenv("IBUS_ADDRESS") != NULL) return;          /* user pinned it */
    gchar *dir = g_build_filename(g_get_user_config_dir(), "ibus", "bus", NULL);
    GDir *d = g_dir_open(dir, 0, NULL);
    if (!d) { g_free(dir); return; }
    const gchar *name;
    gchar *best = NULL; gint64 best_mtime = 0;
    while ((name = g_dir_read_name(d)) != NULL) {
        gchar *path = g_build_filename(dir, name, NULL);
        gchar *buf = NULL;
        if (g_file_get_contents(path, &buf, NULL, NULL)) {
            gint64 mtime = 0;
            GPid pid = 0; gchar *addr = NULL;
            for (gchar *line = strtok(buf, "\n"); line; line = strtok(NULL, "\n")) {
                if (g_str_has_prefix(line, "IBUS_ADDRESS="))
                    { g_free(addr); addr = g_strdup(line + strlen("IBUS_ADDRESS=")); }
                else if (sscanf(line, "IBUS_DAEMON_PID=%d", &pid) == 1) {}
            }
            GStatBuf st;
            if (g_stat(path, &st) == 0) mtime = (gint64)st.st_mtim.tv_sec;
            /* Only trust entries whose daemon still exists (kill 0 = ESRV check). */
            if (addr && pid > 0 && (kill((pid_t)pid, 0) == 0 || errno == EPERM)
                && mtime > best_mtime) {
                g_free(best); best = addr; addr = NULL; best_mtime = mtime;
            } else g_free(addr);
        }
        g_free(buf); g_free(path);
    }
    g_dir_close(d); g_free(dir);
    if (best) {
        g_setenv("IBUS_ADDRESS", best, TRUE);
        fprintf(stderr, "[openglide-ibus] pinned IBUS_ADDRESS from live bus file\n");
        g_free(best);
    }
}

bool og_ibus_start(void) {
    og_resolve_ibus_address();
    g_ctx = g_main_context_new();
    return g_thread_new("openglide-ibus", ibus_thread, NULL) != NULL;
}
bool og_ibus_connected(void) { return g_atomic_int_get(&g_connected) != 0; }
bool og_ibus_active(void)    { return g_atomic_pointer_get(&g_engine) != NULL; }

/* Heap-allocated + FIRE-AND-FORGET: g_main_context_invoke is asynchronous here, so
 * the job must outlive the caller's stack frame — the idle frees it. Never blocks
 * the Qt/UI thread: on the slow T460s a stalled GLib thread let the old ≤0.5 s
 * wait-loop pile up across glide commits and freeze the session. Ordering holds
 * (GLib dispatches invoke sources FIFO), and the injector skips its uinput
 * fallback whenever the engine is active, so a queued commit can't double-fire. */
/* `done` is NULL for fire-and-forget callers; a sync caller points it at its own
 * flag and waits. Ordering matters: uinput backspace lands immediately while an
 * IBus commit is queued, so "commit then backspace" can invert unless the commit
 * is confirmed first. See og_ibus_commit_sync. */
struct commit_job { char *text; volatile int *done; };
static gboolean commit_idle(gpointer p) {
    struct commit_job *j = (struct commit_job *)p;
    /* Re-read g_engine HERE (GLib thread): enable/disable also run on this thread,
     * so the engine is valid-or-NULL — never the caller's pointer, which could
     * dangle if it disabled between the caller's read and this dispatch. */
    IBusEngine *e = (IBusEngine *)g_atomic_pointer_get(&g_engine);
    if (e) {
        IBusText *t = ibus_text_new_from_string(j->text);   /* the validated UTF-8 commit path */
        g_object_ref_sink(t);   /* sink the floating ref so the unref below is valid */
        ibus_engine_commit_text(e, t);
        g_object_unref(t);
    }
    free(j->text);   /* the idle owns + frees the job */
    if (j->done) *j->done = 1;   /* publish AFTER the commit actually ran */
    free(j);
    return G_SOURCE_REMOVE;
}
bool og_ibus_commit(const char *utf8) {
    if (!g_ctx || !g_atomic_pointer_get(&g_engine)) return false;   /* fast-out if inactive */
    struct commit_job *j = malloc(sizeof *j);
    if (!j) return false;
    j->text = strdup(utf8);
    j->done = NULL;
    if (g_ibus_thread_id && g_thread_self() == g_ibus_thread_id)
        commit_idle(j);                                  /* already on the GLib thread */
    else
        g_main_context_invoke(g_ctx, commit_idle, j);   /* async: queued, dispatched in order */
    return true;   /* queued — never blocks the caller */
}

/* Same commit, but waits until it has actually run.
 *
 * ONLY for callers on a dedicated output thread. Blocking was made
 * fire-and-forget because it froze the Qt/UI thread under rapid backspace repeat —
 * that was the right fix at the wrong layer. Once every output op runs on one
 * serialized worker, blocking there costs nothing and buys back ordering against
 * uinput, which writes immediately and would otherwise overtake a queued commit
 * (glide a word, tap backspace instantly, and the delete could land before the
 * word appeared). Never call this from the UI thread. */
bool og_ibus_commit_sync(const char *utf8, int timeout_ms) {
    if (!g_ctx || !g_atomic_pointer_get(&g_engine)) return false;
    if (g_ibus_thread_id && g_thread_self() == g_ibus_thread_id)
        return og_ibus_commit(utf8);          /* inline: already ordered */
    struct commit_job *j = malloc(sizeof *j);
    if (!j) return false;
    volatile int done = 0;
    j->text = strdup(utf8);
    j->done = &done;
    g_main_context_invoke(g_ctx, commit_idle, j);
    for (int i = 0; i < timeout_ms && !done; i++) g_usleep(1000);
    return done != 0;   /* false = timed out; the job still frees itself */
}

/* Restore the user's previous engine (shutdown). Marshaled to the GLib thread. */
static gboolean restore_idle(gpointer p) {
    (void)p;
    if (g_prev && g_bus && IBUS_IS_BUS(g_bus))
        ibus_bus_set_global_engine(g_bus, g_prev);   /* sync, like the probe's finish_cb */
    return G_SOURCE_REMOVE;
}
void og_ibus_shutdown(void) {
    if (g_ctx) g_main_context_invoke(g_ctx, restore_idle, NULL);
    fprintf(stderr, "[openglide-ibus] restored previous engine (%s)\n", g_prev ? g_prev : "(none)");
}
