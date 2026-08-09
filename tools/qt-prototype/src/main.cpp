#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "swipesurface.h"
#include "decoderbridge.h"
#include "injector.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    qmlRegisterType<SwipeSurface>("OpenGlide", 1, 0, "SwipeSurface");

    DecoderBridge decoder;
    Injector injector;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("decoder", &decoder);
    engine.rootContext()->setContextProperty("injector", &injector);
    engine.load(QUrl(QStringLiteral("qrc:/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;
    return app.exec();
}
