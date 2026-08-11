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
#include <glib.h>
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
}

static gpointer ibus_thread(gpointer arg) {
    (void)arg;
    g_ibus_thread_id = g_thread_self();   /* mark this as the IBus/GLib thread */
    g_main_context_push_thread_default(g_ctx);   /* bind the bus to this loop */

    ibus_init();
    g_bus = ibus_bus_new();
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

bool og_ibus_start(void) {
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
struct commit_job { char *text; };
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
    free(j->text);   /* fire-and-forget: the idle owns + frees the job */
    free(j);
    return G_SOURCE_REMOVE;
}
bool og_ibus_commit(const char *utf8) {
    if (!g_ctx || !g_atomic_pointer_get(&g_engine)) return false;   /* fast-out if inactive */
    struct commit_job *j = malloc(sizeof *j);
    if (!j) return false;
    j->text = strdup(utf8);
    if (g_ibus_thread_id && g_thread_self() == g_ibus_thread_id)
        commit_idle(j);                                  /* already on the GLib thread */
    else
        g_main_context_invoke(g_ctx, commit_idle, j);   /* async: queued, dispatched in order */
    return true;   /* queued — never blocks the caller */
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
