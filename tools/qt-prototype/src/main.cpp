#include <QGuiApplication>
#include <QSurfaceFormat>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <csignal>
#include <cstdio>
#include <execinfo.h>
#include <QQuickWindow>
#include <QTimer>
#include <QSocketNotifier>
#include <sys/socket.h>
#include "pointerspeed.h"
#include "windowctl.h"
#include "swipesurface.h"
#include "decoderbridge.h"
#include "injector.h"
#include "appsettings.h"
#include "togglelistener.h"

// Dump a backtrace on crash so we can localize faults in the IBus/GLib path.
static void crash_handler(int sig) {
    // Give the user their pointer speed back before we die. This re-raises with
    // SIG_DFL, so no destructor runs — without this the system mouse stays slowed
    // until the next launch recovers it, with nothing on screen to explain why.
    // Safe here: it only forks and execs pre-rendered arguments.
    og_pointer_emergency_restore();
    void* frames[64];
    int n = backtrace(frames, 64);
    char buf[128];
    int m = std::snprintf(buf, sizeof(buf), "\n=== CRASH signal %d, backtrace ===\n", sig);
    if (m > 0) (void)::write(2, buf, (size_t)m);
    backtrace_symbols_fd(frames, n, 2);
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

int main(int argc, char *argv[]) {
    ::signal(SIGSEGV, crash_handler);
    ::signal(SIGABRT, crash_handler);
    // Translucent window: when "hidden" the keyboard surface stays mapped (GNOME
    // Wayland re-places an unmapped window) but is rendered fully transparent
    // (color "transparent" + content hidden) and made click-through. That needs
    // an alpha channel in the surface format; set it before the platform window
    // integration is created. (QWindow::opacity is a no-op on Qt Wayland.)
    QSurfaceFormat fmt = QSurfaceFormat::defaultFormat();
    fmt.setAlphaBufferSize(8);
    QSurfaceFormat::setDefaultFormat(fmt);

    // Platform selection, BEFORE QGuiApplication — a platform cannot be changed
    // after the integration is constructed. The KDE/GNOME paths are xcb; only
    // wlroots compositors get native Wayland (layer-shell). Without this a bare
    // launch from a .desktop file or a shell on KDE picks native Wayland, where
    // kwin freezes layer-shell margins at surface creation (measured,
    // 2026-08-17) and where Wayland clients cannot self-position at all — the
    // window is born at (0,0) and no drag can move it. Precedence:
    //   1. QT_QPA_PLATFORM already set (session, power user, run.sh) — respected
    //   2. OPENGLIDE_QPA — explicit override, same variable run.sh documents
    //   3. desktop-based default (run.sh's rule, moved in-process)
    {
        const auto desktopHas = [](const char *needle) {
            const QString desk = QString::fromLatin1(qgetenv("XDG_CURRENT_DESKTOP"));
            const auto parts = desk.split(u':');
            for (const QString &d : parts)
                if (d.contains(QLatin1String(needle), Qt::CaseInsensitive))
                    return true;
            return false;
        };
        const bool wlroots = desktopHas("sway") || desktopHas("hyprland")
                          || desktopHas("niri") || desktopHas("river");
        QByteArray why;
        QByteArray chosen;
        if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
            why = QByteArrayLiteral("from environment (respected as-is)");
        } else if (qEnvironmentVariableIsSet("OPENGLIDE_QPA")) {
            chosen = qgetenv("OPENGLIDE_QPA");
            why = QByteArrayLiteral("OPENGLIDE_QPA override");
        } else {
            chosen = wlroots ? QByteArrayLiteral("wayland") : QByteArrayLiteral("xcb");
            why = wlroots ? QByteArrayLiteral("wlroots desktop -> layer-shell")
                          : QByteArrayLiteral("default xcb (override-redirect on KDE, managed on GNOME)");
        }
        if (!chosen.isEmpty())
            qputenv("QT_QPA_PLATFORM", chosen.constData());
        std::fprintf(stderr, "[env] platform: QT_QPA_PLATFORM=%s (%s)\n",
                     qgetenv("QT_QPA_PLATFORM").constData(), why.constData());
    }

    QGuiApplication app(argc, argv);
    qmlRegisterType<SwipeSurface>("OpenGlide", 1, 0, "SwipeSurface");

    DecoderBridge decoder;
    Injector injector;
    AppSettings settings;
    PointerSpeed pointerSpeed;
    pointerSpeed.setLevel(settings.value("pointer/level", 0).toInt());   // opt-in, not opt-out
    pointerSpeed.restoreIfInterrupted();


    // Which input-method framework is this desktop actually using? On GNOME it is
    // IBus (ADR-0002's verified path); KDE normally uses Fcitx5, in which case our
    // in-process IBus engine will never bind no matter what we fix, and text falls
    // back to layout-bound uinput. Log it once so that question is answerable from
    // a run log instead of guesswork.
    {
        const auto env = [](const char *k) {
            const QByteArray v = qgetenv(k);
            return v.isEmpty() ? QByteArrayLiteral("(unset)") : v;
        };
        std::fprintf(stderr,
            "[env] desktop=%s session=%s QT_IM_MODULE=%s GTK_IM_MODULE=%s XMODIFIERS=%s\n",
            env("XDG_CURRENT_DESKTOP").constData(), env("XDG_SESSION_TYPE").constData(),
            env("QT_IM_MODULE").constData(), env("GTK_IM_MODULE").constData(),
            env("XMODIFIERS").constData());
    }

    // Global show/hide trigger (spec §13). Mode + timing come from settings so
    // §13.6 is configurable; if /dev/input isn't readable it stays unavailable
    // and QML refuses to offer the Hidden state.
    ToggleListener toggle;
    toggle.setMode(settings.value("toggle/mode", "chord").toString());
    toggle.setTiming(settings.value("toggle/secondWindowMs", 150).toInt(),
                     settings.value("toggle/holdMs", 300).toInt());
    toggle.start();

    WindowCtl windowCtl;   // layer-shell attach + WM-independent move/resize

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("decoder", &decoder);
    engine.rootContext()->setContextProperty("injector", &injector);
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.rootContext()->setContextProperty("toggleListener", &toggle);
    engine.rootContext()->setContextProperty("pointerSpeed", &pointerSpeed);
    engine.rootContext()->setContextProperty("windowCtl", &windowCtl);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    if (auto *w = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
        windowCtl.attach(w);   // surface config BEFORE the first commit …
        w->show();             // … and only now commit it (see main.qml note)
    }

    // SIGTERM/SIGINT → clean quit so the destructors run and restore BOTH the
    // user's IBus engine (Injector::~Injector) and their pointer speeds
    // (PointerSpeed::~PointerSpeed). A raw kill skips this; the pointer-speed
    // case is additionally recovered by restoreIfInterrupted() next launch.
    static int sigFd[2] = {-1, -1};
    ::socketpair(AF_UNIX, SOCK_STREAM, 0, sigFd);
    auto *sn = new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &app);
    QObject::connect(sn, &QSocketNotifier::activated, &app, []() {
        char a = 0; ::read(sigFd[1], &a, sizeof(a));
        QCoreApplication::quit();
    });
    auto termHandler = [](int) { char a = 1; ssize_t r = ::write(sigFd[0], &a, sizeof(a)); (void)r; };
    ::signal(SIGTERM, termHandler);
    ::signal(SIGINT, termHandler);

    // Slow the real pointer while it is over the keyboard window. Wayland clients
    // can't query the global cursor position (QCursor::pos() is unreliable), so we
    // drive this off the window's Enter/Leave crossing events — the compositor
    // sends them reliably as the pointer crosses the surface. enter()/leave() are
    // idempotent, and a surface born under the cursor also receives an Enter.
    if (auto *window = qobject_cast<QQuickWindow*>(engine.rootObjects().first()))
        window->installEventFilter(&pointerSpeed);

    return app.exec();
}
