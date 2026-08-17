// WindowCtl — platform-agnostic move/resize for the keyboard window.
//
// On the xcb path (GNOME's verified configuration) the WM does the work and
// setX/setY is authoritative. On the Wayland/layer-shell path (KDE, wlroots)
// there IS no WM role for this surface: ShellSurface carries the position in
// margins against top|left anchors, and keyboard-interactivity none guarantees
// the compositor never focuses us — the fix for kwin stealing focus from the
// target app on every press (verified 2026-08-17: uinput text landed in the
// keyboard window instead of the editor).
#pragma once
#include <QObject>
#include <QQuickWindow>
#include <QMargins>

class WindowCtl : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool layerShell READ layerShell CONSTANT)
public:
    explicit WindowCtl(QObject *parent = nullptr) : QObject(parent) {}

    void attach(QQuickWindow *w);          // after the window exists
    bool layerShell() const { return m_shell != nullptr; }

    // Logical position is always win.x/win.y in QML; this pushes it to the
    // platform (margins on layer-shell, setX/setY is already done by then).
    Q_INVOKABLE void move(int x, int y);

    // The position move() last pushed (margins on layer-shell; window x/y
    // otherwise). persistGeometry() should read this — the platform may reset
    // QML window x/y on layer-shell.
    Q_INVOKABLE int posX() const;
    Q_INVOKABLE int posY() const;

private:
    QQuickWindow *m_window = nullptr;
    void *m_shell = nullptr;               // LayerShellQt::Window* when built with it
    int m_px = -1, m_py = -1;              // last position move() pushed
};
