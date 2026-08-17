#include "windowctl.h"
#include <QGuiApplication>
#include <QSettings>
#include <QDebug>

#ifdef OPENGLIDE_HAVE_LAYERSHELL
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>
#endif

void WindowCtl::attach(QQuickWindow *w) {
    m_window = w;
    // KDE/kwin: override-redirect on xcb. An OR window is UNMANAGED — no WM
    // owns it — which buys both properties kwin denied us: free positioning
    // (setX/setY are authoritative; layer-shell margins are frozen at surface
    // creation on kwin, verified 2026-08-17) and no activation on click (the
    // focus-steal that sent glide text into the void was a MANAGED-window
    // behavior). GNOME keeps its verified managed-xcb path; wlroots gets
    // layer-shell (margins are dynamic there). OPENGLIDE_OR=0/1 overrides.
    {
        const QByteArray desk = qgetenv("XDG_CURRENT_DESKTOP");
        const bool kde = desk.contains("KDE");
        const char *force = qEnvironmentVariableIsSet("OPENGLIDE_OR") ? "set" : nullptr;
        const bool wantOR = force ? qgetenv("OPENGLIDE_OR") != "0" : kde;
        if (wantOR && QGuiApplication::platformName() == QLatin1String("xcb")) {
            w->setFlag(Qt::X11BypassWindowManagerHint);
            qInfo() << "[window] override-redirect (unmanaged): free position, "
                       "clicks cannot activate — focus stays on the target app";
        }
    }
#ifdef OPENGLIDE_HAVE_LAYERSHELL
    if (w && QGuiApplication::platformName() == QLatin1String("wayland")) {
        using LSWindow = LayerShellQt::Window;
        // The surface type is fixed at first commit; LayerShellQt's integration
        // handles that (useLayerShell is a no-op since Qt 6.5 — the integration
        // loads with the platform). What matters here is the per-surface config.
        if (LSWindow *s = LSWindow::get(w)) {
            s->setLayer(LSWindow::LayerTop);
            // Top|Left anchors + margins = free positioning: margins carry x/y.
            s->setAnchors(LSWindow::Anchors(LSWindow::AnchorTop | LSWindow::AnchorLeft));
            s->setExclusiveZone(-1);          // never reserve screen space
            // THE point of this path: the compositor must never give us keyboard
            // focus, no matter how the user clicks. Focus stays on the target app
            // and uinput/IBus output reaches it. (kwin stole focus from a
            // does-not-accept-focus xcb window whenever the target was a Wayland
            // window — verified 2026-08-17, the "nothing lands" bug.)
            s->setKeyboardInteractivity(LSWindow::KeyboardInteractivityNone);
            // Position: the QML restore (Component.onCompleted) races attach —
            // window x/y may not hold the restored value yet, and layer-shell
            // margins are the ONLY thing positioning this surface (stuck-at-
            // 0,0 puts the chrome bar under the KDE panel: every click on it
            // lands on the panel instead). Read the persisted geometry directly.
            int px = w->x(), py = w->y();
            QSettings st(QSettings::IniFormat, QSettings::UserScope, "openglide", "openglide");
            if (st.contains("window/x")) px = st.value("window/x").toInt();
            if (st.contains("window/y")) py = st.value("window/y").toInt();
            if (px < 0) px = 0;
            if (py < 0) py = 0;
            // A previous stuck-at-corner session persists 0,0; refuse it — the
            // chrome bar under the panel is how we got stuck in the first place.
            if (px == 0 && py == 0) { px = 200; py = 200; }
            s->setMargins(QMargins(px, py, 0, 0));
            m_px = px; m_py = py;
            m_shell = s;
            qInfo() << "[window] layer-shell margins set from persisted geometry:"
                    << px << py;
            qInfo() << "[window] layer-shell: LayerTop, top|left anchors, "
                       "keyboard-interactivity NONE — focus cannot be stolen";
        } else {
            qWarning() << "[window] Wayland platform but no layer-shell surface "
                          "(compositor support?) — focus preservation NOT guaranteed";
        }
    }
#endif
}

void WindowCtl::move(int x, int y) {
    m_px = x; m_py = y;
#ifdef OPENGLIDE_HAVE_LAYERSHELL
    if (m_shell) {
        static_cast<LayerShellQt::Window *>(m_shell)->setMargins(QMargins(x, y, 0, 0));
        // set_margin only takes effect on the next surface commit, and a pure
        // position change never dirties the scene. Resizing DID move the
        // window (it renders + commits), so force a real frame: 1px nudge out
        // and back — two rendered frames, one carrying the new margins.
        const QSize s = m_window->size();
        m_window->resize(s.width() + 1, s.height());
        m_window->resize(s.width(), s.height());
        return;
    }
#endif
    if (m_window) { m_window->setX(x); m_window->setY(y); }
}
int WindowCtl::posX() const { return m_px >= 0 ? m_px : (m_window ? m_window->x() : 0); }
int WindowCtl::posY() const { return m_py >= 0 ? m_py : (m_window ? m_window->y() : 0); }
