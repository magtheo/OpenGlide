// Injector — commits a word via a Linux uinput virtual keyboard (the layout-bound
// raw-key fallback per ADR-0002). Prototype only: ASCII letters/digits/space;
// arbitrary UTF-8 needs the input-method/IBus path (not wired here yet).
#pragma once
#include <QObject>
#include <QRect>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

class Injector : public QObject {
    Q_OBJECT
public:
    explicit Injector(QObject *parent = nullptr);
    ~Injector();
    // Type `word` as a sequence of key events into whatever holds keyboard focus.
    Q_INVOKABLE bool commit(const QString &word);
    // Commit an exact string verbatim (no added trailing space) — used by undo.
    Q_INVOKABLE bool commitExact(const QString &s);
    // Delete `n` characters (Backspace) from the focused field — used by one-click correction.
    Q_INVOKABLE void backspace(int n);
    // Type a single character (letter/digit/space/comma/period/…) — no trailing space.
    Q_INVOKABLE void typeChar(const QString &ch);
    // OpenGlide is the active IME -> commits route via IBus (UTF-8); else uinput fallback.
    Q_INVOKABLE bool ibusActive() const;
    // Does the focused client support preedit (current word left uncommitted)?
    // -1 capabilities = nothing ever reported. See ibus_engine.h.
    Q_INVOKABLE bool preeditSupported() const;
    Q_INVOKABLE int  capabilities() const;
    // Output ops still queued. 0 = the target field has caught up with the mirror.
    Q_INVOKABLE int  pending() const;
    // Where the focused app says its text cursor is, in screen pixels — used to
    // keep the keyboard off the text being typed. Empty rect if no client has
    // ever reported one (many toolkits never do).
    Q_INVOKABLE QRect caretRect() const;
    // Number of caret reports received; 0 = this app never reports, don't wait.
    Q_INVOKABLE int caretReports() const;

private:
    // ---- serialized output (ADR-0003 single-owner output) -------------------
    // Every op — IBus commit and uinput keystroke alike — runs on ONE worker in
    // submission order. Two reasons, and the second is the load-bearing one:
    //
    //  1. uinput backspace needs ~17 ms of real sleeps per character so the
    //     compositor registers separate keystrokes. On the UI thread that froze
    //     the keyboard, and recent-word correction can delete 70+ characters at
    //     once (delete the word, retype everything after it) — over a second.
    //  2. uinput writes land IMMEDIATELY while an IBus commit is queued onto the
    //     GLib thread, so "commit then backspace" could invert: glide a word, tap
    //     backspace at once, and the delete reaches the field before the word
    //     does. One ordered queue, with the worker WAITING for each IBus commit
    //     (og_ibus_commit_sync), removes the race — it can afford to block.
    struct Op { enum Kind { Commit, CommitExact, TypeChar, Backspace };
                Kind kind = Commit; QString text; int n = 0; };
    void enqueue(const Op &op);
    void workerLoop();
    void runOp(const Op &op);     // worker thread only

    std::deque<Op> m_q;
    mutable std::mutex m_mu;
    std::condition_variable m_cv;
    std::thread m_worker;
    std::atomic<bool> m_quit{false};

    bool setup();                 // create the uinput device once
    void emitKey(int code, int val);
    void rawType(const QString &ch);   // uinput emit of one ASCII char (no IBus dispatch)
    static bool asciiToEvdev(uint32_t cp, int *key, int *shift);

    int m_fd = -1;
};
