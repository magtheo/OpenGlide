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
#include <mutex>
class SwipeEngine;
struct SwipePoint;
struct Candidate;

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

    // points: [{x,y,t}, ...] with t in ms. Results arrive via candidatesReady().
    // Returns TRUE if a worker was launched — i.e. candidatesReady() will fire.
    // Returns FALSE if the glide was refused (engine not ready, or a decode is
    // still running and this one is stale-discarded per ADR-0003). The caller
    // MUST distinguish: a refused glide emits nothing, so treating it as
    // pending leaves the UI waiting on a signal that will never arrive.
    Q_INVOKABLE bool decode(const QVariantList &points);
    // Personalization: record that the user used/chose `word` (boosts it in future decodes).
    Q_INVOKABLE void bumpWord(const QString &word);
    // Corpus-from-real-use (ADR-0006 step 1 / review row 1): the glide `gid`
    // was corrected to `word` — the user's true intent. Appends an amendment
    // line; fold_corpus.py merges these into targets offline. No-op unless
    // OPENGLIDE_LOG_CONTENT recording is on.
    Q_INVOKABLE void amendRecord(qint64 gid, const QString &word);
    // The word from glide `gid` was chip-DELETED: the top-1 was wrong enough to
    // remove, but the intent is unknown — mark the record so fold_corpus.py can
    // exclude it rather than trust a mislabeled top-1.
    Q_INVOKABLE void dropRecord(qint64 gid);

signals:
    void readyChanged();
    void candidatesReady(const QString &greedy, const QVariantList &candidates, double ms, qint64 gid);
    void decoderDied();

private:
    // Parse languages/<lang>/layout.json and install it into the engine. Returns
    // false if the file is missing/malformed — the built-in QWERTY then stands.
    bool loadLayout(const QString &path);
    // Append one JSONL glide record (worker thread; decodes are serialized, but
    // amendRecord can arrive from the UI thread — hence the mutex).
    void recordGlide(qint64 gid, const std::string &greedy,
                     const std::vector<Candidate> &cands,
                     const std::vector<SwipePoint> &pts, double ms);
    void appendLine(const QByteArray &line);

    std::shared_ptr<SwipeEngine> m_eng;     // shared with in-flight worker threads
    std::atomic<bool> m_busy{false};        // serialize decodes (stale-discard)
    bool m_ready = false;
    QVariantList m_keys;                    // {label,x,y} — as handed to QML
    QString m_layoutId = QStringLiteral("builtin_qwerty");
    bool m_record = false;                  // OPENGLIDE_LOG_CONTENT gates glide recording too
    std::mutex m_recMutex;                  // the corpus file is written from two threads
};
