#include "decoderbridge.h"
#include "swipe_engine.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <thread>

// Find the cached FUTO .pte (HF cache layout) relative to the app or in ~/.cache.
static QString resolveModel() {
    const QStringList roots = {
        QCoreApplication::applicationDirPath() + "/../../futo-spike/models/hub",
        QCoreApplication::applicationDirPath() + "/../futo-spike/models/hub",
        QDir::homePath() + "/.cache/huggingface/hub",
        "/home/theo/Documents/coding/repos/OpenGlide/tools/futo-spike/models/hub",
    };
    for (const QString &root : roots) {
        const QString repo = root + "/models--futo-org--futo-swipe/snapshots";
        QDir sd(repo);
        if (!sd.exists()) continue;
        for (const QString &snap : sd.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QString p = repo + "/" + snap + "/honorable_sturgeon/model_fp32.pte";
            if (QFileInfo::exists(p)) return p;
        }
    }
    return {};
}

// Find the word-frequency list (good>god prior) next to the spike dir.
static QString resolveFreq() {
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/../../native-decode-spike/word_freq.txt",
        QCoreApplication::applicationDirPath() + "/../native-decode-spike/word_freq.txt",
        "/home/theo/Documents/coding/repos/OpenGlide/tools/native-decode-spike/word_freq.txt",
    };
    for (const QString &p : candidates) if (QFileInfo::exists(p)) return p;
    return {};
}

DecoderBridge::DecoderBridge(QObject *parent) : QObject(parent) {
    // Load synchronously (~0.1-0.3 s: model load is ~0.1 ms; dictionary load
    // dominates). Brief, at startup, before the window is interactive.
    m_eng = std::make_shared<SwipeEngine>(resolveModel().toStdString(),
                                          "/usr/share/dict/american-english",
                                          resolveFreq().toStdString());
    m_ready = m_eng->ready();
    // Defer the signal so QML (connected after construction) observes the state.
    QTimer::singleShot(0, this, [this] {
        emit readyChanged();
        if (!m_ready) emit decoderDied();
    });
}

DecoderBridge::~DecoderBridge() = default;

void DecoderBridge::decode(const QVariantList &points) {
    if (!m_ready || !m_eng) return;
    // Stale-discard (ADR-0003): drop a new glide if a decode is still running.
    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true)) return;

    std::vector<SwipePoint> pts;
    pts.reserve(points.size());
    for (const QVariant &v : points) {
        const QVariantMap m = v.toMap();
        pts.push_back({(float)m.value("x").toDouble(),
                       (float)m.value("y").toDouble(),
                       (float)m.value("t").toDouble()});
    }
    // Capture a shared_ptr to the engine so a worker outlives any destructor.
    std::shared_ptr<SwipeEngine> eng = m_eng;
    std::thread([this, eng, pts = std::move(pts)]() {
        std::string greedy;
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<Candidate> cands = eng->decode(pts, &greedy);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        QVariantList ql;
        for (const Candidate &c : cands) {
            QVariantMap vm;
            vm["text"] = QString::fromStdString(c.text);
            vm["score"] = (double)c.score;
            ql.append(vm);
        }
        const QString g = QString::fromStdString(greedy);
        QMetaObject::invokeMethod(this, [this, g, ql, ms]() {
            m_busy = false;
            emit candidatesReady(g, ql, ms);
        }, Qt::QueuedConnection);
    }).detach();
}
