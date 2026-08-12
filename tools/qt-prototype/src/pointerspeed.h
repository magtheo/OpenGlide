// PointerSpeed — slows the REAL system pointer (GNOME mouse/touchpad speed via
// gsettings) while the user glides on the keyboard, for finer control with a
// too-lively mouse. Wayland gives an app NO per-window pointer speed (Mutter owns
// the cursor), so this is the global gsetting — the only lever. We capture the
// user's real speeds, restore them on leave/quit, and survive a kill/crash via a
// persisted state flag that the next launch recovers from. The glide capture
// itself stays 1:1 with the cursor (no recorded-path scaling): the visible cursor
// remains honest feedback, and the pointer is what gets slowed.
#pragma once
#include <QObject>

class PointerSpeed : public QObject {
    Q_OBJECT
    // Slowdown level: 0 = off, 1..3 = increasingly slow. Each level subtracts 0.3
    // from the user's base speed (toward slower / more negative), clamped to the
    // gsettings range [-1, 1]. Persisted by the QML caller.
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged)
public:
    explicit PointerSpeed(QObject *parent = nullptr);
    ~PointerSpeed() override;

    int level() const { return m_level; }
    void setLevel(int l);

    // Idempotent. enter() captures the user's current speeds as the base and
    // applies the slowdown; leave() restores them. Called from the window-hover
    // poll in main.cpp.
    Q_INVOKABLE void enter();
    Q_INVOKABLE void leave();

    // Event filter installed on the QQuickWindow: its Enter/Leave crossing events
    // drive enter()/leave() (the only reliable pointer-presence signal on Wayland,
    // where clients can't query the global cursor position).
    bool eventFilter(QObject *o, QEvent *e) override;

    // Call once at startup: if a prior run was killed mid-override, the speed is
    // still slowed — restore the saved base speeds from the state flag.
    Q_INVOKABLE void restoreIfInterrupted();

signals:
    void levelChanged();

private:
    void apply();      // write the slowed speed for the current level/base
    void restore();    // write back the saved base speeds
    void markActive(bool on);  // persist override state for crash recovery
    static double clamp1(double v) { return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v); }
    double targetFor(double base) const { return clamp1(base - m_level * 0.3); }

    int m_level = 2;            // 0=off, 1..3 increasing slowdown
    bool m_overriding = false;  // currently holding the slowed speed
    double m_mouseBase = 0.0;   // user's real mouse speed, captured on enter
    double m_touchBase = 0.0;   // user's real touchpad speed, captured on enter
};
