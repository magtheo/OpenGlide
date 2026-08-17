#include "injector.h"
#include "ibus_engine.h"
#include <QByteArray>
#include <linux/uinput.h>
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <QThread>

static void msleep(int ms) {
    struct timespec ts = { ms/1000, (long)(ms%1000)*1000000L };
    nanosleep(&ts, NULL);
}
static void write_all(int fd, const void *buf, size_t n) {
    ssize_t r = write(fd, buf, n);
    (void)r;  // best-effort; device may be tearing down
}

Injector::Injector(QObject *parent) : QObject(parent) {
    setup();              // create the uinput device on this thread, before the worker
    og_ibus_start();
    m_worker = std::thread([this] { workerLoop(); });
}
Injector::~Injector() {
    { std::lock_guard<std::mutex> lk(m_mu); m_quit = true; }
    m_cv.notify_all();
    if (m_worker.joinable()) m_worker.join();   // drains what is already queued
    og_ibus_shutdown();   /* restore the user's previous IBus engine */
    if (m_fd >= 0) { ioctl(m_fd, UI_DEV_DESTROY); close(m_fd); }
}

void Injector::enqueue(const Op &op) {
    { std::lock_guard<std::mutex> lk(m_mu); m_q.push_back(op); }
    m_cv.notify_one();
}

int Injector::pending() const {
    std::lock_guard<std::mutex> lk(m_mu);
    return int(m_q.size());
}

void Injector::workerLoop() {
    for (;;) {
        Op op;
        {
            std::unique_lock<std::mutex> lk(m_mu);
            m_cv.wait(lk, [this] { return m_quit || !m_q.empty(); });
            if (m_q.empty()) return;            // quit AND drained
            op = m_q.front();
            m_q.pop_front();
        }
        runOp(op);
    }
}

// Worker thread only. Blocking here is fine and deliberate — it is what keeps
// uinput and IBus output in submission order.
void Injector::runOp(const Op &op) {
    switch (op.kind) {
    case Op::Commit: {
        const QString withSep = op.text + QChar(' ');   // trailing separator (spec §9.2)
        if (og_ibus_text_capable() && og_ibus_active() && og_ibus_commit_sync(withSep.toUtf8().constData(), 500)) return;
        if (m_fd < 0 && !setup()) return;
        for (const QChar qc : op.text) rawType(QString(qc));
        rawType(" ");
        return;
    }
    case Op::CommitExact:
        if (og_ibus_text_capable() && og_ibus_active() && og_ibus_commit_sync(op.text.toUtf8().constData(), 500)) return;
        if (m_fd < 0 && !setup()) return;
        for (const QChar qc : op.text) rawType(QString(qc));
        return;
    case Op::TypeChar:
        if (og_ibus_text_capable() && og_ibus_active() && og_ibus_commit_sync(op.text.toUtf8().constData(), 500)) return;
        rawType(op.text);
        return;
    case Op::Backspace: {
        // uinput ONLY. delete_surrounding_text (the IBus delete API) SIGABRTs
        // gnome-shell even WITH valid surrounding-text state, and
        // ibus_engine_forward_key_event(BackSpace) doesn't delete. uinput is the
        // only mechanism that doesn't fault the shell. It targets the focused
        // surface (not the IBus commit context), so it's imprecise and can
        // diverge — but it's non-crashing. The real fix is preedit; see
        // og_ibus_preedit_supported().
        if (m_fd < 0 && !setup()) return;
        for (int i = 0; i < op.n; i++) {
            emitKey(KEY_BACKSPACE, 1);
            QThread::usleep(5000);    // hold — a real keystroke, not a 0-duration blip
            emitKey(KEY_BACKSPACE, 0);
            QThread::usleep(12000);   // gap — separate keystrokes so the compositor
                                      // registers each (2 ms lost ~1 of 4 backspaces)
        }
        return;
    }
    }
}

// IMPORTANT: evdev KEY_* values are physical-keyboard scancodes (QWERTY order),
// NOT alphabetical (KEY_A=30, KEY_S=31, … KEY_B=48). Use an explicit table.
bool Injector::asciiToEvdev(uint32_t cp, int *key, int *shift) {
    *shift = 0;
    static const int letters[26] = {
        KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,
        KEY_K,KEY_L,KEY_M,KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,
        KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z
    };
    static const int digits[10] = {
        KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9
    };
    if (cp >= 'a' && cp <= 'z') { *key = letters[cp - 'a']; return true; }
    if (cp >= 'A' && cp <= 'Z') { *key = letters[cp - 'A']; *shift = 1; return true; }
    if (cp >= '0' && cp <= '9') { *key = digits[cp - '0']; return true; }
    switch (cp) {
        case ' ':  *key = KEY_SPACE;  return true;
        case '.':  *key = KEY_DOT;    return true;
        case ',':  *key = KEY_COMMA;  return true;
        case '\n': *key = KEY_ENTER;  return true;
        case '-':  *key = KEY_MINUS;  return true;
        default:   return false;
    }
}

void Injector::emitKey(int code, int val) {
    if (m_fd < 0) return;
    struct input_event ev; memset(&ev, 0, sizeof ev);
    ev.type = EV_KEY; ev.code = code; ev.value = val;
    write_all(m_fd, &ev, sizeof ev);
    struct input_event syn; memset(&syn, 0, sizeof syn);
    syn.type = EV_SYN; syn.code = SYN_REPORT; syn.value = 0;
    write_all(m_fd, &syn, sizeof syn);
}

bool Injector::setup() {
    m_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (m_fd < 0) {
        fprintf(stderr, "[injector] /dev/uinput open failed: %s\n", strerror(errno));
        return false;
    }
    ioctl(m_fd, UI_SET_EVBIT, EV_KEY);
    int keys[] = {
        KEY_A,KEY_B,KEY_C,KEY_D,KEY_E,KEY_F,KEY_G,KEY_H,KEY_I,KEY_J,KEY_K,KEY_L,KEY_M,
        KEY_N,KEY_O,KEY_P,KEY_Q,KEY_R,KEY_S,KEY_T,KEY_U,KEY_V,KEY_W,KEY_X,KEY_Y,KEY_Z,
        KEY_0,KEY_1,KEY_2,KEY_3,KEY_4,KEY_5,KEY_6,KEY_7,KEY_8,KEY_9,
        KEY_SPACE,KEY_DOT,KEY_COMMA,KEY_ENTER,KEY_MINUS,KEY_LEFTSHIFT,KEY_BACKSPACE
    };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++)
        ioctl(m_fd, UI_SET_KEYBIT, keys[i]);
    struct uinput_setup us; memset(&us, 0, sizeof us);
    us.id.bustype = BUS_USB; us.id.vendor = 0x8888; us.id.product = 0x0002; us.id.version = 1;
    strncpy(us.name, "openglide-qt-injector", UINPUT_MAX_NAME_SIZE);
    if (ioctl(m_fd, UI_DEV_SETUP, &us) < 0 || ioctl(m_fd, UI_DEV_CREATE) < 0) {
        fprintf(stderr, "[injector] uinput device create failed: %s\n", strerror(errno));
        close(m_fd); m_fd = -1; return false;
    }
    msleep(300);
    fprintf(stderr, "[injector] uinput keyboard ready (text will land as raw keys)\n");
    return true;
}

void Injector::rawType(const QString &ch) {
    if (m_fd < 0 && !setup()) return;
    if (ch.isEmpty()) return;
    int key, shift;
    if (!asciiToEvdev((uint32_t)ch.at(0).toLatin1(), &key, &shift)) return;
    if (shift) emitKey(KEY_LEFTSHIFT, 1);
    emitKey(key, 1);
    emitKey(key, 0);
    if (shift) emitKey(KEY_LEFTSHIFT, 0);
    QThread::usleep(2000);
}

// The four public ops are now non-blocking: they queue and return. Order is
// preserved by the single worker, so callers can keep treating them as immediate.
void Injector::typeChar(const QString &ch) {
    if (ch.isEmpty()) return;
    enqueue({Op::TypeChar, ch, 0});
}

// ADR-0002: commit via IBus (UTF-8, layout-independent) when OpenGlide is the
// active IME; fall back to raw-key uinput (layout-bound, ASCII). Returns true
// meaning "queued" — the result of the op itself is no longer synchronous.
bool Injector::commit(const QString &word) {
    if (word.isEmpty()) return true;
    enqueue({Op::Commit, word, 0});
    return true;
}

// Undo / retype path: the exact string, no added space, as ONE op.
bool Injector::commitExact(const QString &s) {
    if (s.isEmpty()) return true;
    enqueue({Op::CommitExact, s, 0});
    return true;
}

bool Injector::ibusActive() const { return og_ibus_active(); }
bool Injector::ibusTextCapable() const { return og_ibus_text_capable(); }

QRect Injector::caretRect() const {
    int x = 0, y = 0, w = 0, h = 0;
    if (!og_ibus_cursor_rect(&x, &y, &w, &h)) return {};
    return QRect(x, y, w, h);
}

int Injector::caretReports() const { return int(og_ibus_cursor_reports()); }
bool Injector::preeditSupported() const { return og_ibus_preedit_supported(); }
int  Injector::targetGeneration() const { return og_ibus_target_generation(); }
bool Injector::focused() const { return og_ibus_focused(); }
int  Injector::capabilities() const { return og_ibus_capabilities(); }
void Injector::backspace(int n) {
    if (n <= 0) return;
    enqueue({Op::Backspace, QString(), n});
}
