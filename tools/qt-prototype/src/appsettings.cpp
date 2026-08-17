#include "appsettings.h"

AppSettings::AppSettings(QObject *parent)
    : QObject(parent),
      m_s(QSettings::IniFormat, QSettings::UserScope,
          QStringLiteral("openglide"), QStringLiteral("openglide")) {}

QVariant AppSettings::value(const QString &key, const QVariant &fallback) const {
    return m_s.value(key, fallback);
}

void AppSettings::setValue(const QString &key, const QVariant &v) {
    m_s.setValue(key, v);
}

void AppSettings::sync() { m_s.sync(); }
