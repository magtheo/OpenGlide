// DecoderBridge — talks to the persistent futo_server.py over QProcess stdio.
// QML calls decode(points); emits candidatesReady(greedy, candidates, ms).
#pragma once
#include <QObject>
#include <QVariantList>
#include <QProcess>

class DecoderBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
public:
    explicit DecoderBridge(QObject *parent = nullptr);
    bool ready() const { return m_ready; }

    // points: [{x,y,t}, ...] with t in ms. Sent to the server as one JSON line.
    Q_INVOKABLE void decode(const QVariantList &points);

signals:
    void readyChanged();
    void candidatesReady(const QString &greedy, const QVariantList &candidates, double ms);
    void decoderDied();   // futo_server exited unexpectedly — decode can never complete

private slots:
    void onStderr();
    void onStdout();
    void onFinished(int exitCode, QProcess::ExitStatus status);

private:
    QProcess *m_proc = nullptr;
    bool m_ready = false;
    QByteArray m_outBuf;
};
