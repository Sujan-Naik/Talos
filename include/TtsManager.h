#ifndef TTSMANAGER_H
#define TTSMANAGER_H

#include <QObject>
#include <QThread>
#include <QMutex>
#include <QBuffer>
#include <QQueue>
#include <QAudioSink>
#include <QAudioFormat>
#include <QDateTime>
#include <atomic>
#include <memory>

#include "c-api.h"

// Forward declaration of sherpa-onnx internal struct
struct SherpaOnnxOfflineTts;

// Struct representing a synthesized audio chunk
struct AudioChunk {
    QByteArray data;
    int sampleRate{0};
    int speakerId{0};
    qint64 timestamp{0};
};

// --- TtsWorker ---
// Worker object executed inside the dedicated background QThread
class TtsWorker : public QObject {
    Q_OBJECT

public:
    explicit TtsWorker(QObject *parent = nullptr);
    ~TtsWorker() override;

    int getSampleRate() const { return m_sampleRate; }

public slots:
    // Marked as slot so QMetaObject::invokeMethod string lookup can find it across threads
    bool initModel(const QString &modelPath, const QString &tokensPath,
                   const QString &voicesPath, const QString &dataDir);
    void synthesizeSentence(const QString &text, int sid = 0);

signals:
    void audioGenerated(const QByteArray &pcmData, int sampleRate, int speakerId);
    void synthesisError(const QString &error);

private:
    const SherpaOnnxOfflineTts *m_tts{nullptr};
    int m_sampleRate{0};
    mutable QMutex m_ttsMutex;
};

// --- TtsManager ---
// High-level thread-safe manager for queuing TTS requests and playing back audio
class TtsManager : public QObject {
    Q_OBJECT

public:
    explicit TtsManager(QObject *parent = nullptr);
    ~TtsManager() override;

    bool initialize(const QString &modelDir);
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled.load(); }
    bool isInitialized() const { return m_initialized.load(); }

    void enqueueSentence(const QString &sentence, int speakerId = 0);
    void stopAndClear();
    int queueSize() const;

signals:
    void requestSynthesis(const QString &text, int speakerId);
    void playbackFinished();
    void errorOccurred(const QString &errorMsg);

private slots:
    void handleAudioGenerated(const QByteArray &pcmData, int sampleRate, int speakerId);
    void handleSynthesisError(const QString &error);
    void playNextInQueue();

private:
    void cleanupAudioSink();
    bool validateModelFiles(const QString &modelDir);

    QThread m_workerThread;
    TtsWorker *m_worker{nullptr};

    std::atomic<bool> m_enabled{true};
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_isPlaying{false};
    std::atomic<uint64_t> m_generation{0};

    int m_defaultSampleRate{22050};

    // Queue & Thread Safety
    mutable QMutex m_queueMutex;
    QQueue<AudioChunk> m_audioQueue;

    // Audio Output
    std::unique_ptr<QAudioSink> m_audioSink;
    QBuffer m_currentAudioBuffer;
    QMutex m_audioBufferMutex;
};

#endif // TTSMANAGER_H