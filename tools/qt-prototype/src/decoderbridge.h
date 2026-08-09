// DecoderBridge — native FUTO decoder (replaces the QProcess/Python futo_server).
// Owns a SwipeEngine (ExecuTorch C++); decode() runs on a worker thread, serialized
// by a busy flag (stale-discard per ADR-0003). QML-facing API is unchanged from the
// Python version: ready / decode / candidatesReady / decoderDied.
#pragma once
#include <QObject>
#include <QVariantList>
#include <atomic>
#include <memory>
class SwipeEngine;

class DecoderBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
public:
    explicit DecoderBridge(QObject *parent = nullptr);
    ~DecoderBridge();
    bool ready() const { return m_ready; }

    // points: [{x,y,t}, ...] with t in ms. Returns via candidatesReady().
    Q_INVOKABLE void decode(const QVariantList &points);

signals:
    void readyChanged();
    void candidatesReady(const QString &greedy, const QVariantList &candidates, double ms);
    void decoderDied();

private:
    std::shared_ptr<SwipeEngine> m_eng;     // shared with in-flight worker threads
    std::atomic<bool> m_busy{false};        // serialize decodes (stale-discard)
    bool m_ready = false;
};
