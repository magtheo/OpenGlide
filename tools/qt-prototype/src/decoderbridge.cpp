#include "decoderbridge.h"
#include "swipe_engine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <cstdio>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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

// Find the system dictionary (decode lexicon). Debian names it american-english;
// Fedora names it words. First existing candidate wins; empty if none.
static QString resolveDict() {
    const QStringList candidates = {
        "/usr/share/dict/american-english",
        "/usr/share/dict/words",
        "/usr/share/dict/british-english",
    };
    for (const QString &p : candidates) if (QFileInfo::exists(p)) return p;
    return {};
}

// Per-user word counts (personalization) -> ~/.local/share/openglide/user_freq.tsv.
static QString resolveUserFreq() {
    QDir d(QDir::homePath() + "/.local/share/openglide");
    d.mkpath(".");   // ensure the dir exists
    return d.absoluteFilePath("user_freq.tsv");
}

// Find languages/<lang>/layout.json — the shared key geometry (ADR-0005 step 0).
static QString resolveLayout() {
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/../../../languages/en/layout.json",
        QCoreApplication::applicationDirPath() + "/../../languages/en/layout.json",
        QDir::currentPath() + "/languages/en/layout.json",
    };
    for (const QString &p : candidates) if (QFileInfo::exists(p)) return p;
    return {};
}

bool DecoderBridge::loadLayout(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        std::fprintf(stderr, "[layout] %s: %s\n", qPrintable(path), qPrintable(err.errorString()));
        return false;
    }
    const QJsonObject root = doc.object();
    std::vector<KeyCenter> keys;
    QVariantList forQml;
    for (const QJsonValue &v : root.value("keys").toArray()) {
        const QJsonObject k = v.toObject();
        const QString label = k.value("label").toString();
        if (label.size() != 1) continue;
        const float x = float(k.value("x").toDouble());
        const float y = float(k.value("y").toDouble());
        keys.push_back({label.at(0).toLatin1(), x, y});
        QVariantMap m;
        m["l"] = label.toUpper();     // display glyph
        m["c"] = label.toLower();     // what a tap types
        m["x"] = x;
        m["y"] = y;
        forQml.append(m);
    }
    // Install into the engine FIRST — if it rejects the geometry (missing or
    // duplicate letters), keep the built-in and do not hand QML a layout the
    // decoder is not actually using.
    if (!m_eng || !m_eng->set_layout(keys)) {
        std::fprintf(stderr, "[layout] %s rejected (%zu keys) — using built-in QWERTY\n",
                     qPrintable(path), keys.size());
        return false;
    }
    m_keys = forQml;
    m_layoutId = root.value("layout_id").toString(QStringLiteral("unknown"));
    std::fprintf(stderr, "[layout] %s: %lld keys (%s)\n", qPrintable(path),
                 (long long)forQml.size(), qPrintable(m_layoutId));
    return true;
}

DecoderBridge::DecoderBridge(QObject *parent) : QObject(parent) {
    // Corpus-from-real-use (ADR-0006 step 1): glides are recorded under the same
    // content opt-in as keystroke logging (ADR-0004) — a recorded corpus IS user
    // content. Unlike the stderr key log this writes a FILE that outlives the
    // session, so say so loudly and say where it is.
    {
        const QByteArray v = qgetenv("OPENGLIDE_LOG_CONTENT");
        const QByteArray a = qgetenv("OPENGLIDE_KEY_DEBUG");
        m_record = ((!v.isEmpty() && v != "0") || (!a.isEmpty() && a != "0"));
        if (m_record)
            std::fprintf(stderr,
                "*** OPENGLIDE CORPUS RECORDING IS ON ***\n"
                "    Every glide (points + candidates + your corrections) is\n"
                "    appended to ~/.local/share/openglide/corpus-live.jsonl.\n"
                "    That file contains what you typed — delete it any time.\n"
                "    Unset OPENGLIDE_LOG_CONTENT and restart to stop recording.\n\n");
    }
    // Load synchronously (~0.1-0.3 s: model load is ~0.1 ms; dictionary load
    // dominates). Brief, at startup, before the window is interactive.
    m_eng = std::make_shared<SwipeEngine>(resolveModel().toStdString(),
                                          resolveDict().toStdString(),
                                          resolveFreq().toStdString(),
                                          resolveUserFreq().toStdString());
    m_ready = m_eng->ready();

    // Key geometry: one file, both consumers (spec §7.2). If it can't be loaded
    // the engine keeps its built-in QWERTY, and we hand QML that same geometry
    // read back from the engine — so the two can never disagree.
    const QString lp = resolveLayout();
    if (lp.isEmpty() || !loadLayout(lp)) {
        m_keys.clear();
        for (const KeyCenter &k : m_eng->layout()) {
            const QString c = QString(QChar::fromLatin1(k.label));
            QVariantMap m;
            m["l"] = c.toUpper();
            m["c"] = c;
            m["x"] = k.x;
            m["y"] = k.y;
            m_keys.append(m);
        }
    }

    // Defer the signal so QML (connected after construction) observes the state.
    QTimer::singleShot(0, this, [this] {
        emit readyChanged();
        if (!m_ready) emit decoderDied();
    });
}

DecoderBridge::~DecoderBridge() {
    if (m_eng) m_eng->save_user_freq();   // persist personalization on shutdown
}

void DecoderBridge::bumpWord(const QString &word) {
    if (m_eng) m_eng->bump(word.toStdString());
}

void DecoderBridge::appendLine(const QByteArray &line) {
    std::lock_guard<std::mutex> lk(m_recMutex);
    QDir d(QDir::homePath() + "/.local/share/openglide");
    d.mkpath(".");
    QFile f(d.absoluteFilePath("corpus-live.jsonl"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append)) return;
    f.write(line);
    f.write("\n");
}

void DecoderBridge::recordGlide(qint64 gid, const std::string &greedy,
                                const std::vector<Candidate> &cands,
                                const std::vector<SwipePoint> &pts, double ms) {
    if (!m_record) return;
    // data-formats.md corpus schema, plus glide_id and greedy. `target` is the
    // best guess at decode time (top-1); amendRecord lines carry the user's
    // corrections and fold_corpus.py merges them — a corpus label is only
    // trustworthy once it survived (or was corrected by) the user.
    QJsonObject root;
    root["schema_version"] = 1;
    root["glide_id"] = (double)gid;        // epoch ms — exact as a double
    const QString top1 = cands.empty() ? QString()
                                        : QString::fromStdString(cands[0].text);
    root["target"] = top1;
    root["layout_id"] = m_layoutId;
    root["greedy"] = QString::fromStdString(greedy);
    QJsonArray pa;
    for (const SwipePoint &p : pts)
        pa.append(QJsonObject{{"x", (double)p.x}, {"y", (double)p.y},
                              {"time_us", (double)p.t * 1000.0}});
    root["points"] = pa;
    QJsonArray ca;
    for (const Candidate &c : cands)
        ca.append(QJsonObject{{"text", QString::fromStdString(c.text)},
                              {"score", (double)c.score},
                              {"source", "SwipeEngine"}});
    root["candidates"] = ca;
    root["selected"] = top1;
    root["decode_ms"] = ms;
    appendLine(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void DecoderBridge::amendRecord(qint64 gid, const QString &word) {
    if (!m_record || gid <= 0) return;
    appendLine(QJsonDocument(QJsonObject{
        {"amend_of", (double)gid}, {"selected", word.toLower()}
    }).toJson(QJsonDocument::Compact));
}

void DecoderBridge::dropRecord(qint64 gid) {
    if (!m_record || gid <= 0) return;
    appendLine(QJsonDocument(QJsonObject{{"drop_of", (double)gid}}
    ).toJson(QJsonDocument::Compact));
}

bool DecoderBridge::decode(const QVariantList &points) {
    if (!m_ready || !m_eng) return false;
    // Stale-discard (ADR-0003): drop a new glide if a decode is still running.
    // Report the drop rather than swallowing it — the caller has no other way
    // to learn that no candidatesReady() is coming.
    bool expected = false;
    if (!m_busy.compare_exchange_strong(expected, true)) return false;

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
    const qint64 gid = QDateTime::currentMSecsSinceEpoch();
    std::thread([this, eng, pts = std::move(pts), gid]() {
        std::string greedy;
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<Candidate> cands = eng->decode(pts, &greedy);
        const double ms = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - t0).count();
        std::fprintf(stderr, "[decode] greedy=\"%s\" %.0fms:", greedy.c_str(), ms);
        for (const Candidate &c : cands) std::fprintf(stderr, " %s(%.2f)", c.text.c_str(), c.score);
        std::fputc('\n', stderr);
        recordGlide(gid, greedy, cands, pts, ms);
        QVariantList ql;
        for (const Candidate &c : cands) {
            QVariantMap vm;
            vm["text"] = QString::fromStdString(c.text);
            vm["score"] = (double)c.score;
            ql.append(vm);
        }
        const QString g = QString::fromStdString(greedy);
        QMetaObject::invokeMethod(this, [this, g, ql, ms, gid]() {
            m_busy = false;
            emit candidatesReady(g, ql, ms, gid);
        }, Qt::QueuedConnection);
    }).detach();
    return true;
}
