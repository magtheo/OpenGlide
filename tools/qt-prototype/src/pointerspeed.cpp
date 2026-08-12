#include <glib.h>
#include <gio/gio.h>
#include "pointerspeed.h"
#include <QSettings>
#include <QDebug>
#include <QEvent>

// The only Wayland pointer-speed lever: GNOME's peripherals gsettings, which
// Mutter applies to libinput live. Global, so we always save + restore.
static const char *MOUSE = "org.gnome.desktop.peripherals.mouse";
static const char *TOUCH = "org.gnome.desktop.peripherals.touchpad";
static const char *KEY = "speed";

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
    const auto t = e->type();
    if (t == QEvent::Enter)       enter();
    else if (t == QEvent::Leave)  leave();
    return QObject::eventFilter(o, e);
}

PointerSpeed::PointerSpeed(QObject *parent) : QObject(parent) {}

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
    if (m_overriding || m_level == 0) return;
    m_mouseBase = gsGet(MOUSE);
    m_touchBase = gsGet(TOUCH);
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
    gsSet(MOUSE, targetFor(m_mouseBase));
    gsSet(TOUCH, targetFor(m_touchBase));
}

void PointerSpeed::restore() {
    gsSet(MOUSE, clamp1(m_mouseBase));
    gsSet(TOUCH, clamp1(m_touchBase));
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
    QSettings s(QSettings::IniFormat, QSettings::UserScope, "openglide", "openglide");
    if (s.value("pointer/overrideActive", false).toBool()) {
        const double mb = s.value("pointer/mouseBase", 0.0).toDouble();
        const double tb = s.value("pointer/touchBase", 0.0).toDouble();
        gsSet(MOUSE, clamp1(mb));
        gsSet(TOUCH, clamp1(tb));
        s.setValue("pointer/overrideActive", false);
        s.sync();
        qWarning() << "[ptr] restored pointer speeds after interrupted override"
                   << "(mouse" << mb << "touch" << tb << ")";
    }
}
