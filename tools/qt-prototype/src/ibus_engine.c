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
static void og_enable(IBusEngine *engine) {
    g_atomic_pointer_set(&g_engine, engine);        /* now bound to the focused context */
    fprintf(stderr, "[openglide-ibus] enabled — commits route via IBus\n");
    if (getenv("OPENGLIDE_IBUS_TEST")) {            /* self-test: commit UTF-8 right at enable */
        bool ok = og_ibus_commit("æøå 🫐 openglide");
        fprintf(stderr, "[openglide-ibus] TEST og_ibus_commit -> %d\n", ok);
    }
}
static void og_disable(IBusEngine *engine) {
    (void)engine;
    g_atomic_pointer_set(&g_engine, NULL);
    fprintf(stderr, "[openglide-ibus] disabled\n");
}
/* Pass-through: forward every physical key event to the client so normal typing
 * keeps working while OpenGlide is the active IME (else it would absorb keys). */
static gboolean og_process_key_event(IBusEngine *engine, guint keyval, guint keycode, guint state) {
    ibus_engine_forward_key_event(engine, keyval, keycode, state);
    return TRUE;
}
static void og_engine_init(OgEngine *self) { (void)self; }
static void og_engine_class_init(OgEngineClass *klass) {
    IBUS_ENGINE_CLASS(klass)->enable            = og_enable;
    IBUS_ENGINE_CLASS(klass)->disable           = og_disable;
    IBUS_ENGINE_CLASS(klass)->process_key_event = og_process_key_event;
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

struct commit_job { const char *text; IBusEngine *engine; int done; };
static gboolean commit_idle(gpointer p) {
    struct commit_job *j = (struct commit_job *)p;
    IBusText *t = ibus_text_new_from_string(j->text);   /* the validated UTF-8 commit path */
    ibus_engine_commit_text(j->engine, t);
    g_object_unref(t);
    j->done = 1;
    return G_SOURCE_REMOVE;
}
bool og_ibus_commit(const char *utf8) {
    IBusEngine *e = (IBusEngine *)g_atomic_pointer_get(&g_engine);
    if (!e || !g_ctx) return false;
    struct commit_job j = { utf8, e, 0 };
    if (g_ibus_thread_id && g_thread_self() == g_ibus_thread_id)
        commit_idle(&j);                              /* already on the IBus thread */
    else
        g_main_context_invoke(g_ctx, commit_idle, &j);  /* cross-thread: synchronous */
    return j.done != 0;
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
