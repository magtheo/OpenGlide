// AppSettings — a thin QSettings bridge so the window remembers where it was and
// how big it was (ADR-0005 §4). Deliberately not QtCore's QML Settings type: that
// needs the QtCore QML module at runtime, and this app links Qt6::Core anyway.
// Local-only, no telemetry (ADR-0004): ~/.config/openglide/openglide.conf.
#pragma once
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariant>

class AppSettings : public QObject {
    Q_OBJECT
public:
    explicit AppSettings(QObject *parent = nullptr);
    Q_INVOKABLE QVariant value(const QString &key, const QVariant &fallback = {}) const;
    Q_INVOKABLE void setValue(const QString &key, const QVariant &v);
    Q_INVOKABLE void sync();

private:
    mutable QSettings m_s;
};
