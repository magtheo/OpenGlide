#include "togglelistener.h"

#include <QDir>
#include <QMetaObject>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace {

constexpr int kBitsPerLong = int(sizeof(long) * 8);
inline bool testBit(int bit, const unsigned long *arr) {
    return (arr[bit / kBitsPerLong] >> (bit % kBitsPerLong)) & 1ul;
}

qint64 nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// A device we care about: reports EV_KEY and has a left mouse button.
bool isPointer(int fd) {
    unsigned long evbits[(EV_MAX / kBitsPerLong) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0) return false;
    if (!testBit(EV_KEY, evbits)) return false;
    unsigned long keybits[(KEY_MAX / kBitsPerLong) + 1] = {0};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits) < 0) return false;
    return testBit(BTN_LEFT, keybits);
}

} // namespace

ToggleListener::ToggleListener(QObject *parent) : QObject(parent) {}

ToggleListener::~ToggleListener() { stop(); }

void ToggleListener::setState(bool avail, const QString &why) {
    // Called from the ctor/start on the Qt thread only.
    m_available = avail;
    m_status = why;
    emit stateChanged();
}

void ToggleListener::setMode(const QString &m) {
    m_mode = m;
    if (m == QLatin1String("middle"))      m_modeCode = 1;
    else if (m == QLatin1String("mouse4")) m_modeCode = 2;
    else if (m == QLatin1String("mouse5")) m_modeCode = 3;
    else { m_mode = QStringLiteral("chord"); m_modeCode = 0; }
    emit stateChanged();
}

void ToggleListener::setTiming(int secondButtonWindowMs, int holdMs) {
    if (secondButtonWindowMs > 0) m_secondWindowMs = secondButtonWindowMs;
    if (holdMs > 0) m_holdMs = holdMs;
}

void ToggleListener::start() {
    if (m_thread.joinable()) return;

    QDir d(QStringLiteral("/dev/input"));
    const QStringList names = d.entryList({QStringLiteral("event*")}, QDir::System | QDir::Files);
    int denied = 0;
    for (const QString &n : names) {
        const QByteArray path = d.absoluteFilePath(n).toLocal8Bit();
        const int fd = ::open(path.constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) { if (errno == EACCES || errno == EPERM) denied++; continue; }
        if (isPointer(fd)) m_fds.push_back(fd);   // never EVIOCGRAB (ADR-0004)
        else ::close(fd);
    }

    if (m_fds.empty()) {
        setState(false, denied > 0
            ? QStringLiteral("no read access to /dev/input (add your user to group 'input', then log out and back in)")
            : QStringLiteral("no mouse device found under /dev/input"));
        return;
    }
    if (::pipe(m_wake) != 0) {
        for (int fd : m_fds) ::close(fd);
        m_fds.clear();
        setState(false, QStringLiteral("could not create the wake pipe"));
        return;
    }

    m_quit = false;
    m_thread = std::thread([this] { run(); });
    setState(true, QStringLiteral("watching %1 device(s)").arg(int(m_fds.size())));
    std::fprintf(stderr, "[toggle] observing %zu pointer device(s), mode=%s\n",
                 m_fds.size(), qPrintable(m_mode));
}

void ToggleListener::stop() {
    if (!m_thread.joinable()) return;
    m_quit = true;
    if (m_wake[1] >= 0) { const char b = 1; ssize_t r = ::write(m_wake[1], &b, 1); (void)r; }
    m_thread.join();
    for (int fd : m_fds) ::close(fd);
    m_fds.clear();
    if (m_wake[0] >= 0) ::close(m_wake[0]);
    if (m_wake[1] >= 0) ::close(m_wake[1]);
    m_wake[0] = m_wake[1] = -1;
}

void ToggleListener::run() {
    std::vector<pollfd> pfds;
    pfds.reserve(m_fds.size() + 1);
    for (int fd : m_fds) pfds.push_back({fd, POLLIN, 0});
    pfds.push_back({m_wake[0], POLLIN, 0});

    ChordDetector chord;
    chord.secondWindowMs = m_secondWindowMs.load();
    chord.holdMs = m_holdMs.load();

    const auto fire = [this] {
        QMetaObject::invokeMethod(this, [this] { emit toggleRequested(); }, Qt::QueuedConnection);
    };

    while (!m_quit) {
        // Poll with a short timeout: the hold threshold has to be detected even
        // when no further events arrive (the user is holding perfectly still).
        const int timeout = chord.waiting() ? 20 : 250;
        const int rc = ::poll(pfds.data(), pfds.size(), timeout);
        if (rc < 0 && errno != EINTR) break;
        if (m_quit) break;

        for (size_t i = 0; i + 1 < pfds.size(); i++) {
            if (!(pfds[i].revents & POLLIN)) continue;
            input_event ev[32];
            for (;;) {
                const ssize_t n = ::read(pfds[i].fd, ev, sizeof(ev));
                if (n <= 0) break;
                const int count = int(n / ssize_t(sizeof(input_event)));
                for (int k = 0; k < count; k++) {
                    if (ev[k].type != EV_KEY) continue;
                    const bool down = ev[k].value != 0;   // 1 press, 2 autorepeat
                    const int code = ev[k].code;

                    const int mode = m_modeCode.load();
                    if (mode != 0) {
                        // Single-button modes: fire on press, nothing to time.
                        const int want = mode == 1 ? BTN_MIDDLE
                                       : mode == 2 ? BTN_SIDE : BTN_EXTRA;
                        if (code == want && ev[k].value == 1) fire();
                        continue;
                    }

                    if (code != BTN_LEFT && code != BTN_RIGHT) continue;
                    chord.button(code == BTN_LEFT, down, nowMs());
                }
            }
        }

        // Held long enough? The hold threshold is what keeps ordinary clicking
        // from toggling the keyboard.
        if (chord.tick(nowMs())) fire();
    }
}
