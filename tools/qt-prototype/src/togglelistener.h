// ToggleListener — the global mouse-only show/hide trigger (spec §13.1-§13.4).
//
// Watches raw evdev mouse buttons on a worker thread so the toggle works even
// when OpenGlide has no surface at all (spec §13.2: a hidden window cannot
// receive pointer events). Per ADR-0004 this is deliberately OBSERVE-ONLY — it
// never EVIOCGRABs and never consumes events, so the application underneath
// still sees the clicks. Whether that is annoying enough to justify an
// interception layer is the §13.3 question, and it can only be answered by
// living with this version first.
//
// Requires read access to /dev/input/event* (group `input`). When it cannot get
// that, `available` stays false and `status` says why — the UI must then refuse
// to offer the Hidden state, because hiding with no way back is the same trap as
// a one-click quit (ADR-0005 §5).
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <atomic>
#include <thread>
#include <vector>

// The LMB+RMB chord as a pure state machine (spec §13.1), deliberately separated
// from the evdev plumbing so it can be exercised without /dev/input: feed it
// button transitions, call tick() on a timer, and it reports the gesture exactly
// once per chord.
//
// Rules: either order counts; the second button must arrive inside
// secondWindowMs; both must then stay down for holdMs (that hold is what stops
// ordinary clicking from toggling the keyboard); and it re-arms only after BOTH
// buttons come back up, so one chord can never fire twice.
struct ChordDetector {
    int secondWindowMs = 150;
    int holdMs = 300;

    void button(bool isLeft, bool down, qint64 now) {
        const bool wasBoth = leftDown && rightDown;
        if (isLeft) leftDown = down; else rightDown = down;

        if (!leftDown && !rightDown) {                 // fully released: re-arm
            armed = false; fired = false; firstDownAt = 0; bothDownAt = 0;
        } else if (leftDown && rightDown && !wasBoth) { // the second one joined
            bothDownAt = now;
            armed = (firstDownAt != 0) && (now - firstDownAt <= secondWindowMs);
        } else if ((leftDown || rightDown) && firstDownAt == 0) {
            firstDownAt = now;                          // the first of the pair
        }
    }

    // True exactly once, when the hold threshold is crossed.
    bool tick(qint64 now) {
        if (!armed || fired || !leftDown || !rightDown) return false;
        if (now - bothDownAt < holdMs) return false;
        fired = true;
        return true;
    }

    // Waiting on a hold — poll faster so the threshold isn't detected late.
    bool waiting() const { return armed && !fired; }

    bool leftDown = false, rightDown = false;
    qint64 firstDownAt = 0, bothDownAt = 0;
    bool armed = false, fired = false;
};

class ToggleListener : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY stateChanged)
public:
    explicit ToggleListener(QObject *parent = nullptr);
    ~ToggleListener();

    bool available() const { return m_available; }
    QString status() const { return m_status; }
    QString mode() const { return m_mode; }

    // "chord" (LMB+RMB, the spec default) | "middle" | "mouse4" | "mouse5".
    Q_INVOKABLE void setMode(const QString &m);
    // Chord timing (spec §13.6). Both in ms.
    Q_INVOKABLE void setTiming(int secondButtonWindowMs, int holdMs);
    // Open the devices and start watching. Safe to call twice.
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();

signals:
    void stateChanged();
    // The configured gesture fired. Emitted on the Qt thread.
    void toggleRequested();

private:
    void run();                       // worker: poll() over every mouse fd
    void setState(bool avail, const QString &why);

    std::thread m_thread;
    std::atomic<bool> m_quit{false};
    int m_wake[2] = {-1, -1};         // self-pipe so stop() interrupts poll()
    std::vector<int> m_fds;

    bool m_available = false;
    QString m_status = QStringLiteral("not started");
    QString m_mode = QStringLiteral("chord");
    std::atomic<int> m_secondWindowMs{150};
    std::atomic<int> m_holdMs{300};
    std::atomic<int> m_modeCode{0};   // 0 chord · 1 middle · 2 mouse4 · 3 mouse5
};
