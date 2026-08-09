#include "decoderbridge.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>

// Paths (prototype on this machine). venv python + HF model cache live in futo-spike.
static const char *PY = "/home/theo/Documents/coding/repos/OpenGlide/tools/futo-spike/.venv/bin/python";
static const char *SERVER = "/home/theo/Documents/coding/repos/OpenGlide/tools/qt-prototype/futo_server.py";
static const char *HF_HOME = "/home/theo/Documents/coding/repos/OpenGlide/tools/futo-spike/models";

DecoderBridge::DecoderBridge(QObject *parent) : QObject(parent) {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HF_HOME", HF_HOME);
    env.insert("HF_HUB_DISABLE_PROGRESS_BARS", "1");
    env.insert("LC_ALL", "C.UTF-8");
    env.insert("PYTHONUNBUFFERED", "1");

    m_proc = new QProcess(this);
    m_proc->setProcessEnvironment(env);
    m_proc->setProcessChannelMode(QProcess::MergedChannels); // stderr+stdout merged -> read via stderr slot? no
    // We want stderr (READY) and stdout (results) separately:
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_proc, &QProcess::readyReadStandardError, this, &DecoderBridge::onStderr);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &DecoderBridge::onStdout);
    m_proc->start(PY, QStringList{SERVER});
}

void DecoderBridge::onStderr() {
    // futo_server prints "READY" on stderr once the model+dict are loaded.
    QByteArray b = m_proc->readAllStandardError();
    if (!m_ready && b.contains("READY")) {
        m_ready = true;
        emit readyChanged();
    }
    fputs(b.constData(), stderr);
}

void DecoderBridge::onStdout() {
    m_outBuf += m_proc->readAllStandardOutput();
    int idx;
    while ((idx = m_outBuf.indexOf('\n')) >= 0) {
        QByteArray line = m_outBuf.left(idx).trimmed();
        m_outBuf = m_outBuf.mid(idx + 1);
        if (line.isEmpty()) continue;
        QJsonParseError err;
        QJsonDocument doc = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError) continue;
        QJsonObject o = doc.object();
        QString greedy = o.value("greedy").toString();
        double ms = o.value("ms").toDouble();
        QVariantList cands;
        for (const QJsonValue &v : o.value("candidates").toArray()) {
            QVariantMap m;
            QJsonObject co = v.toObject();
            m["text"] = co.value("text").toString();
            m["score"] = co.value("score").toDouble();
            cands.append(m);
        }
        emit candidatesReady(greedy, cands, ms);
    }
}

void DecoderBridge::decode(const QVariantList &points) {
    if (!m_ready || !m_proc || m_proc->state() != QProcess::Running) return;
    QJsonArray arr;
    for (const QVariant &v : points) {
        QVariantMap pm = v.toMap();
        QJsonObject p;
        p["x"] = pm.value("x").toDouble();
        p["y"] = pm.value("y").toDouble();
        p["t"] = pm.value("t").toDouble();
        arr.append(p);
    }
    QJsonObject req;
    req["points"] = arr;
    QByteArray line = QJsonDocument(req).toJson(QJsonDocument::Compact);
    line.append('\n');
    m_proc->write(line);
}
