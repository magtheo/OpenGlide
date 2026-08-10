#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <csignal>
#include <cstdio>
#include <execinfo.h>
#include <unistd.h>
#include "swipesurface.h"
#include "decoderbridge.h"
#include "injector.h"
#include "appsettings.h"

// Dump a backtrace on crash so we can localize faults in the IBus/GLib path.
static void crash_handler(int sig) {
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
    QGuiApplication app(argc, argv);
    qmlRegisterType<SwipeSurface>("OpenGlide", 1, 0, "SwipeSurface");

    DecoderBridge decoder;
    Injector injector;
    AppSettings settings;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("decoder", &decoder);
    engine.rootContext()->setContextProperty("injector", &injector);
    engine.rootContext()->setContextProperty("settings", &settings);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
