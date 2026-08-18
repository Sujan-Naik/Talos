#ifndef CHATBACKEND_H
#define CHATBACKEND_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>

class TtsManager;

/**
 * ChatBackend bridges the UI, TTS, and the LLM stream.
 * It handles sentence splitting, markdown cleaning, and queuing TTS requests.
 */
class ChatBackend : public QObject
{
    Q_OBJECT

public:
    explicit ChatBackend(QObject *parent = nullptr);
    ~ChatBackend();

    /** Check if TTS is fully initialised and ready. */
    bool isTtsReady() const;

public slots:
    /** Speak a complete text immediately (enqueues it). */
    void speak(const QString &text);

    /** Stop all ongoing speech and clear the queue. */
    void stopSpeech();

    /** Enable/disable TTS output. */
    void onTtsToggled(bool enabled);

    /** Called when the user sends a message. */
    void onUserSendMessage(const QString &message);

    /** Called when a new delta arrives from the AI stream. */
    void handleAiStreamDelta(const QString &deltaText);

    /** Called when the AI stream finishes. */
    void handleAiStreamFinished();

    /** Request a screen capture. */
    void requestCapture();

    /** Request to toggle the microphone. */
    void requestToggleMic();

signals:
    void messageReceived(const QString &message);
    void captureRequested();
    void micToggleRequested();
    void ttsError(const QString &error);

private slots:
    void onTtsServerReady();
    void handleTtsSentenceFinished();
    void handleTtsError(const QString &error);

private:
    // TTS management
    TtsManager *m_tts;
    bool        m_ttsInitialized;      // true only after serverReady
    bool        m_ttsEnabled;
    bool        m_ttsProcessing;

    QString     m_ttsBuffer;           // accumulates incoming delta text
    QStringList m_ttsQueue;            // sentences waiting to be sent to TTS
    QList<QString> m_pendingTtsSentences; // sentences queued before server ready

    // Internal helpers
    void setupTts();
    void splitAndEnqueueSentences();
    void enqueueTtsSentence(const QString &text);
    void processTtsQueue();
    void flushTtsBuffer();
    QString cleanMarkdown(const QString &text) const;
};

#endif // CHATBACKEND_H