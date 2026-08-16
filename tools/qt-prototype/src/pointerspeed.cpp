#include <glib.h>
#include <gio/gio.h>
#include "pointerspeed.h"
#include <QSettings>
#include <QDebug>
#include <QEvent>
#include <csignal>
#include <cstdio>
#include <unistd.h>

extern char **environ;

// The only Wayland pointer-speed lever: GNOME's peripherals gsettings, which
// Mutter applies to libinput live. Global, so we always save + restore.
static const char *MOUSE = "org.gnome.desktop.peripherals.mouse";
static const char *TOUCH = "org.gnome.desktop.peripherals.touchpad";
static const char *KEY = "speed";

// g_settings_new() treats a missing schema as a PROGRAMMER ERROR and aborts the
// process (verified: GLib-GIO-ERROR + SIGTRAP). The schemas here ship with GNOME,
// so on KDE/Sway/bare WMs they may simply not exist — look before leaping.
static bool schemaExists(const char *id) {
    GSettingsSchemaSource *src = g_settings_schema_source_get_default();
    if (!src) return false;
    GSettingsSchema *sc = g_settings_schema_source_lookup(src, id, TRUE);
    if (!sc) return false;
    g_settings_schema_unref(sc);
    return true;
}

// ---- async-signal-safe emergency restore ----------------------------------
// Rendered ahead of time so the handler itself only forks and execs.
static char g_gsBin[64] = {0};        // absolute gsettings path, resolved at ctor
static char g_mouseArg[32] = {0};     // rendered in apply()
static char g_touchArg[32] = {0};
static volatile sig_atomic_t g_emActive = 0;
static volatile sig_atomic_t g_emMouse = 0;
static volatile sig_atomic_t g_emTouch = 0;

static void emExec(const char *schema, char *val) {
    const pid_t pid = fork();          // async-signal-safe
    if (pid != 0) return;              // parent, or fork failed: nothing more to do
    char *const argv[] = { g_gsBin, (char *)"set", (char *)schema,
                           (char *)KEY, val, nullptr };
    execve(g_gsBin, argv, environ);    // async-signal-safe
    _exit(127);
}

extern "C" void og_pointer_emergency_restore(void) {
    if (!g_emActive || !g_gsBin[0]) return;
    g_emActive = 0;                    // once only, even if we re-enter
    if (g_emMouse) emExec(MOUSE, g_mouseArg);
    if (g_emTouch) emExec(TOUCH, g_touchArg);
    // Deliberately no waitpid: the caller is about to die and the children are
    // reparented to init, where the exec still completes.
}

static double gsGet(const char *schema) {
    GSettings *s = g_settings_new(schema);
    double v = g_settings_get_double(s, KEY);
    g_object_unref(s);
    return v;
}
static void gsSet(const char *schema, double v) {
    GSettings *s = g_settings_new(schema);
    g_settings_set_double(s, KEY, v);
    g_object_unref(s);
}

bool PointerSpeed::eventFilter(QObject *o, QEvent *e) {
    if (!available()) return QObject::eventFilter(o, e);
    const auto t = e->type();
    if (t == QEvent::Enter)       enter();
    else if (t == QEvent::Leave)  leave();
    return QObject::eventFilter(o, e);
}

PointerSpeed::PointerSpeed(QObject *parent) : QObject(parent) {
    m_haveMouse = schemaExists(MOUSE);
    m_haveTouch = schemaExists(TOUCH);
    g_emMouse = m_haveMouse ? 1 : 0;
    g_emTouch = m_haveTouch ? 1 : 0;
    if (!available()) {
        qWarning() << "[ptr] GNOME peripherals schemas absent — pointer slowdown "
                      "unavailable on this desktop (this is expected off GNOME)";
        return;
    }
    for (const char *c : {"/usr/bin/gsettings", "/bin/gsettings", "/usr/local/bin/gsettings"}) {
        if (::access(c, X_OK) == 0) { std::snprintf(g_gsBin, sizeof g_gsBin, "%s", c); break; }
    }
}

PointerSpeed::~PointerSpeed() {
    // Clean quit / app exec returning: give the user their real speed back.
    if (m_overriding) { restore(); markActive(false); }
}

void PointerSpeed::setLevel(int l) {
    if (l < 0) l = 0;
    if (l > 3) l = 3;
    if (l == m_level) return;
    m_level = l;
    emit levelChanged();
    if (m_overriding) {
        if (m_level == 0) { restore(); markActive(false); m_overriding = false; }
        else              { apply(); }
    }
}

void PointerSpeed::enter() {
    if (m_overriding || m_level == 0 || !available()) return;
    if (m_haveMouse) m_mouseBase = gsGet(MOUSE);
    if (m_haveTouch) m_touchBase = gsGet(TOUCH);
    markActive(true);
    apply();
    m_overriding = true;
    qDebug() << "[ptr] enter L" << m_level << ": mouse" << m_mouseBase << "->"
             << targetFor(m_mouseBase) << " touch" << m_touchBase << "->" << targetFor(m_touchBase);
}

void PointerSpeed::leave() {
    if (!m_overriding) return;
    restore();
    markActive(false);
    m_overriding = false;
    qDebug() << "[ptr] leave: restored mouse" << m_mouseBase << "touch" << m_touchBase;
}

void PointerSpeed::apply() {
    if (m_haveMouse) gsSet(MOUSE, targetFor(m_mouseBase));
    if (m_haveTouch) gsSet(TOUCH, targetFor(m_touchBase));
    // Pre-render the restore arguments while it is still safe to allocate, so a
    // crash handler can fork/exec them without touching GLib or the heap.
    std::snprintf(g_mouseArg, sizeof g_mouseArg, "%.6f", clamp1(m_mouseBase));
    std::snprintf(g_touchArg, sizeof g_touchArg, "%.6f", clamp1(m_touchBase));
    g_emActive = 1;
}

void PointerSpeed::restore() {
    g_emActive = 0;                    // clear first: we are restoring properly
    if (m_haveMouse) gsSet(MOUSE, clamp1(m_mouseBase));
    if (m_haveTouch) gsSet(TOUCH, clamp1(m_touchBase));
}

void PointerSpeed::markActive(bool on) {
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "openglide", "openglide");
    s.setValue("pointer/overrideActive", on);
    if (on) {
        s.setValue("pointer/mouseBase", m_mouseBase);
        s.setValue("pointer/touchBase", m_touchBase);
    }
    s.sync();
}

void PointerSpeed::restoreIfInterrupted() {
    if (!available()) return;
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "openglide", "openglide");
    if (s.value("pointer/overrideActive", false).toBool()) {
        const double mb = s.value("pointer/mouseBase", 0.0).toDouble();
        const double tb = s.value("pointer/touchBase", 0.0).toDouble();
        if (m_haveMouse) gsSet(MOUSE, clamp1(mb));
        if (m_haveTouch) gsSet(TOUCH, clamp1(tb));
        s.setValue("pointer/overrideActive", false);
        s.sync();
        qWarning() << "[ptr] restored pointer speeds after interrupted override"
                   << "(mouse" << mb << "touch" << tb << ")";
    }
}
