#include "injector.h"
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

Injector::Injector(QObject *parent) : QObject(parent) { setup(); }
Injector::~Injector() {
    if (m_fd >= 0) { ioctl(m_fd, UI_DEV_DESTROY); close(m_fd); }
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
    if (m_fd < 0) return false;
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
        close(m_fd); m_fd = -1; return false;
    }
    msleep(300);
    return true;
}

bool Injector::commit(const QString &word) {
    if (m_fd < 0 && !setup()) return false;
    for (QChar qc : word) {
        int key, shift;
        if (!asciiToEvdev((uint32_t)qc.toLatin1(), &key, &shift)) continue;
        if (shift) emitKey(KEY_LEFTSHIFT, 1);
        emitKey(key, 1);
        emitKey(key, 0);
        if (shift) emitKey(KEY_LEFTSHIFT, 0);
        QThread::usleep(2000);
    }
    emitKey(KEY_SPACE, 1); emitKey(KEY_SPACE, 0);   // trailing separator (spec §9.2)
    return true;
}

void Injector::backspace(int n) {
    if (m_fd < 0 && !setup()) return;
    for (int i = 0; i < n; i++) {
        emitKey(KEY_BACKSPACE, 1);
        emitKey(KEY_BACKSPACE, 0);
        QThread::usleep(2000);
    }
}
