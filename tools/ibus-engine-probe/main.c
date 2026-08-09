/* ibus-engine-probe — validate the ADR-0002 primary path on GNOME: an
 * OpenGlide IBus engine commits ARBITRARY UTF-8 to the focused field via
 * ibus_engine_commit_text (the one cell the text-output-probe left open —
 * GNOME/mutter does not expose zwp_input_method_v2, so IBus is the path).
 *
 * Self-contained & transient: registers a throwaway engine "openglide-probe"
 * with the running ibus-daemon, activates itself as the GLOBAL engine for a
 * moment, commits the test string on `enable`, restores the previous global
 * engine, and exits. No installed files, no permanent session changes.
 *
 *   Build: make
 *   Run:   focus a text field (e.g. gnome-text-editor), then
 *          ./ibus-probe ["<optional utf-8 string>"]
 *
 * Exit 0 = committed; 2 = registered but enable never fired (no focused IM
 * context, or GNOME blocked the switch); 3 = could not connect to ibus-daemon.
 *
 * Why the engine switch is ASYNC: ibus_bus_set_global_engine() is a synchronous
 * D-Bus round-trip. Issued before the main loop runs, it deadlocks — ibus-daemon
 * calls CreateEngine back into our factory, which our loop can't dispatch while
 * we're blocked. So we set_global_engine_async from an idle once the loop runs.
 */
#include <ibus.h>
#include <glib.h>
#include <glib-object.h>
#include <stdio.h>
#include <string.h>

/* ---- a minimal IBusEngine subclass whose `enable` commits the string ---- */
#define OG_TYPE_ENGINE  (og_engine_get_type())
typedef struct _OgEngine      OgEngine;
typedef struct _OgEngineClass OgEngineClass;
struct _OgEngine      { IBusEngine parent; };
struct _OgEngineClass { IBusEngineClass parent_class; };

static GType og_engine_get_type(void);
G_DEFINE_TYPE(OgEngine, og_engine, IBUS_TYPE_ENGINE)

static const char *g_text      = "blåbær æøå 日本 🫐";
static IBusBus    *g_bus       = NULL;
static gchar      *g_prev      = NULL;
static GMainLoop  *g_loop      = NULL;
static gboolean    g_committed = FALSE;
static gboolean    g_done      = FALSE;
static gboolean    g_engine_built = FALSE;

static gboolean finish_cb(gpointer user_data) {
    if (g_done) return G_SOURCE_REMOVE;
    g_done = TRUE;
    if (g_prev && g_bus && IBUS_IS_BUS(g_bus))
        ibus_bus_set_global_engine(g_bus, g_prev);   /* restore the user's engine */
    if (g_loop) g_main_loop_quit(g_loop);
    return G_SOURCE_REMOVE;
}

static void og_enable(IBusEngine *engine) {
    if (g_committed) return;
    g_committed = TRUE;
    IBusText *t = ibus_text_new_from_string(g_text);
    ibus_engine_commit_text(engine, t);              /* the real commit path */
    g_object_unref(t);
    fprintf(stderr, "[openglide-probe] enable: commit_text(%zu bytes) = \"%s\"\n",
            strlen(g_text), g_text);
    g_timeout_add(700, finish_cb, NULL);             /* let the commit flush, then restore */
}

static void og_engine_init(OgEngine *self) {
    g_engine_built = TRUE;
    fprintf(stderr, "[openglide-probe] factory built OgEngine (ibus-daemon activated us)\n");
}
static void og_engine_class_init(OgEngineClass *klass) {
    IBUS_ENGINE_CLASS(klass)->enable = og_enable;
}

static void set_engine_cb(GObject *src, GAsyncResult *res, gpointer user_data) {
    GError *err = NULL;
    gboolean ok = ibus_bus_set_global_engine_async_finish(g_bus, res, &err);
    fprintf(stderr, "[openglide-probe] set_global_engine_async -> %d", ok);
    if (err) { fprintf(stderr, "  err=%s", err->message); g_error_free(err); }
    fputc('\n', stderr);
}

static gboolean kick_cb(gpointer user_data) {
    ibus_bus_set_global_engine_async(g_bus, "openglide-probe", -1, NULL, set_engine_cb, NULL);
    return G_SOURCE_REMOVE;
}

int main(int argc, char **argv) {
    if (argc > 1 && argv[1][0]) g_text = argv[1];

    ibus_init();
    g_bus = ibus_bus_new();
    if (!ibus_bus_is_connected(g_bus)) {
        fprintf(stderr, "[openglide-probe] not connected to ibus-daemon\n");
        return 3;
    }
    fprintf(stderr, "[openglide-probe] connected to ibus-daemon\n");

    IBusEngineDesc *cur = ibus_bus_get_global_engine(g_bus);
    if (cur) g_prev = g_strdup(ibus_engine_desc_get_name(cur));
    fprintf(stderr, "[openglide-probe] current global engine = %s\n", g_prev ? g_prev : "(none)");

    IBusFactory *factory = ibus_factory_new(ibus_bus_get_connection(g_bus));
    ibus_factory_add_engine(factory, "openglide-probe", OG_TYPE_ENGINE);
    guint32 rn = ibus_bus_request_name(g_bus, "org.freedesktop.IBus.OpenGlide.Probe", 0);
    fprintf(stderr, "[openglide-probe] request_name -> %u\n", (unsigned)rn);

    IBusComponent *comp = ibus_component_new_varargs(
        "name", "org.freedesktop.IBus.OpenGlide.Probe",
        "description", "OpenGlide IBus commit probe",
        "version", "0.1", "license", "MIT",
        "author", "OpenGlide", "homepage", "https://openglide.example",
        "command_line", "ibus-probe", "textdomain", "ibus",
        NULL);
    /* all string fields populated so register_component serialization has no NULLs */
    IBusEngineDesc *desc = ibus_engine_desc_new_varargs(
        "name", "openglide-probe", "longname", "OpenGlide Probe",
        "description", "OpenGlide commit probe", "language", "en",
        "license", "MIT", "author", "OpenGlide", "icon", "",
        "layout", "default", "layout_variant", "", "layout_option", "",
        "rank", "0", "symbol", "", "setup", "", "hotkeys", "",
        "version", "0.1", "textdomain", "ibus",
        NULL);
    ibus_component_add_engine(comp, desc);
    gboolean rc = ibus_bus_register_component(g_bus, comp);
    fprintf(stderr, "[openglide-probe] register_component -> %d\n", rc);

    g_loop = g_main_loop_new(NULL, FALSE);
    g_timeout_add_seconds(6, finish_cb, NULL);       /* safety: never strand the engine */
    g_idle_add(kick_cb, NULL);                        /* async switch once the loop runs */
    g_main_loop_run(g_loop);

    fprintf(stderr, "[openglide-probe] done committed=%d engine_built=%d restored=%s\n",
            g_committed, g_engine_built, g_prev ? g_prev : "(none)");
    return g_committed ? 0 : 2;
}
