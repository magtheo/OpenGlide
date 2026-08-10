// DecoderBridge — native FUTO decoder (replaces the QProcess/Python futo_server).
// Owns a SwipeEngine (ExecuTorch C++); decode() runs on a worker thread, serialized
// by a busy flag (stale-discard per ADR-0003). QML-facing API is unchanged from the
// Python version: ready / decode / candidatesReady / decoderDied.
#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <atomic>
#include <memory>
class SwipeEngine;

class DecoderBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    // The glide geometry, read back from the engine after the layout is installed
    // (spec §7.2). QML draws from this instead of keeping its own copy, so the keys
    // on screen and the keys the decoder scores against cannot drift apart.
    Q_PROPERTY(QVariantList keys READ keys CONSTANT)
    Q_PROPERTY(QString layoutId READ layoutId CONSTANT)
public:
    explicit DecoderBridge(QObject *parent = nullptr);
    ~DecoderBridge();
    bool ready() const { return m_ready; }
    QVariantList keys() const { return m_keys; }
    QString layoutId() const { return m_layoutId; }

    // points: [{x,y,t}, ...] with t in ms. Returns via candidatesReady().
    Q_INVOKABLE void decode(const QVariantList &points);
    // Personalization: record that the user used/chose `word` (boosts it in future decodes).
    Q_INVOKABLE void bumpWord(const QString &word);

signals:
    void readyChanged();
    void candidatesReady(const QString &greedy, const QVariantList &candidates, double ms);
    void decoderDied();

private:
    // Parse languages/<lang>/layout.json and install it into the engine. Returns
    // false if the file is missing/malformed — the built-in QWERTY then stands.
    bool loadLayout(const QString &path);

    std::shared_ptr<SwipeEngine> m_eng;     // shared with in-flight worker threads
    std::atomic<bool> m_busy{false};        // serialize decodes (stale-discard)
    bool m_ready = false;
    QVariantList m_keys;                    // {label,x,y} — as handed to QML
    QString m_layoutId = QStringLiteral("builtin_qwerty");
};
