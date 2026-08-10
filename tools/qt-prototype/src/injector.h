// Injector — commits a word via a Linux uinput virtual keyboard (the layout-bound
// raw-key fallback per ADR-0002). Prototype only: ASCII letters/digits/space;
// arbitrary UTF-8 needs the input-method/IBus path (not wired here yet).
#pragma once
#include <QObject>
#include <QString>

class Injector : public QObject {
    Q_OBJECT
public:
    explicit Injector(QObject *parent = nullptr);
    ~Injector();
    // Type `word` as a sequence of key events into whatever holds keyboard focus.
    Q_INVOKABLE bool commit(const QString &word);
    // Delete `n` characters (Backspace) from the focused field — used by one-click correction.
    Q_INVOKABLE void backspace(int n);
    // Type a single character (letter/digit/space/comma/period/…) — no trailing space.
    Q_INVOKABLE void typeChar(const QString &ch);
    // OpenGlide is the active IME -> commits route via IBus (UTF-8); else uinput fallback.
    Q_INVOKABLE bool ibusActive() const;

private:
    bool setup();                 // create the uinput device once
    void emitKey(int code, int val);
    void rawType(const QString &ch);   // uinput emit of one ASCII char (no IBus dispatch)
    static bool asciiToEvdev(uint32_t cp, int *key, int *shift);

    int m_fd = -1;
};
