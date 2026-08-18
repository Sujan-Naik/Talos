#ifndef TTSMANAGER_H
#define TTSMANAGER_H

#include <QObject>
#include <QQueue>
#include <QAudioSink>
#include <QBuffer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QUrl>
#include <QMutex>
#include <QAtomicInteger>
#include <QDateTime>
#include <QProcess>
#include <QTimer>

struct AudioChunk {
    QByteArray data;
    int sampleRate;
    int speakerId;
    qint64 timestamp;
};

class TtsManager : public QObject
{
    Q_OBJECT

public:
    explicit TtsManager(QObject *parent = nullptr);
    ~TtsManager();

    bool initialize(const QString &serverUrl = QString(), bool autoStart = true);
    void setEnabled(bool enabled);
    void enqueueSentence(const QString &sentence, int speakerId = 0);
    void stopAndClear();
    int queueSize() const;
    void stopDockerContainer();

signals:
    void sentenceQueued(int speakerId);
    void sentenceFinished();
    void errorOccurred(const QString &error);
    void serverReady();

private slots:
    void onTtsReplyFinished();
    void playNextInQueue();
    void onAudioStateChanged(QAudio::State state);
    void cleanupAudioSink();

    void onDockerPullFinished(int exitCode, QProcess::ExitStatus status);
    void onDockerRunFinished(int exitCode, QProcess::ExitStatus status);
    void onDockerOutputReady();
    void onDockerErrorReady();
    void checkServerHealth();

private:
    // Core processing – used by both direct reply and manager slot
    void processReply(QNetworkReply *reply);

    void requestSynthesis(const QString &text, int speakerId);
    bool checkDockerAvailable();
    void startDockerContainer();
    void pullDockerImage();
    void runDockerContainer();
    void waitForServer();

    // Members
    QNetworkAccessManager *m_networkManager;
    QUrl m_serverUrl;
    QNetworkReply *m_currentReply;

    std::unique_ptr<QAudioSink> m_audioSink;
    QBuffer m_audioBuffer;
    QMutex m_audioBufferMutex;

    QQueue<AudioChunk> m_audioQueue;
    mutable QMutex m_queueMutex;

    QAtomicInteger<bool> m_enabled;
    QAtomicInteger<bool> m_initialized;
    QAtomicInteger<bool> m_isPlaying;

    QProcess *m_dockerProcess;
    bool m_containerStarted;
    QString m_containerId;
    QTimer *m_healthCheckTimer;
    int m_healthAttempts;

    // Docker configuration
    static constexpr const char* DOCKER_IMAGE = "ghcr.io/remsky/kokoro-fastapi-rocm:latest";
    static constexpr int HOST_PORT = 8880;
    static constexpr int CONTAINER_PORT = 8880;
    static constexpr const char* GFX_VERSION = "10.3.0";   // for RX 6700 XT
    static constexpr int MAX_HEALTH_ATTEMPTS = 60;         // 120 seconds (2s interval)

    // TTS voice mapping (speakerId -> voice name)
    static constexpr const char* DEFAULT_VOICE = "af_bella";
};

#endif // TTSMANAGER_H