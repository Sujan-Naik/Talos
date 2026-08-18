#ifndef CHATBACKEND_H
#define CHATBACKEND_H

#include <QObject>
#include <QStringList>

class TtsManager;

class ChatBackend : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool ttsEnabled
               READ isTtsEnabled
               NOTIFY ttsEnabledChanged)

    Q_PROPERTY(QString ttsVoice
               READ ttsVoice
               WRITE setTtsVoice
               NOTIFY ttsVoiceChanged)

    Q_PROPERTY(QStringList ttsVoices
               READ ttsVoices
               NOTIFY ttsVoicesChanged)

public:
    explicit ChatBackend(QObject *parent = nullptr);
    ~ChatBackend() override;

    bool isTtsReady() const;
    bool isTtsEnabled() const;

    QString ttsVoice() const;
    QStringList ttsVoices() const;

    Q_INVOKABLE void setTtsVoice(const QString &voice);
    Q_INVOKABLE void onTtsToggled(bool enabled);
    Q_INVOKABLE void previewTtsVoice();

public slots:
    void onUserSendMessage(const QString &message);
    void handleAiStreamDelta(const QString &deltaText);
    void handleAiStreamFinished();

    void requestCapture();
    void requestToggleMic();

    void speak(const QString &text);
    void stopSpeech();

signals:
    void messageReceived(const QString &message);

    void captureRequested();
    void micToggleRequested();

    void ttsError(const QString &error);

    void ttsEnabledChanged(bool enabled);
    void ttsVoiceChanged(const QString &voice);
    void ttsVoicesChanged(const QStringList &voices);

private slots:
    void onTtsServerReady();
    void handleTtsSentenceFinished();
    void handleTtsError(const QString &error);
    void handleTtsVoicesChanged(const QStringList &voices);

private:
    void setupTts();

    void enqueueTtsSentence(const QString &text);
    void processTtsQueue();
    void splitAndEnqueueSentences();
    void flushTtsBuffer();

    QString cleanMarkdown(const QString &text) const;

    TtsManager *m_tts = nullptr;

    bool m_ttsInitialized = false;
    bool m_ttsEnabled = false;
    bool m_ttsProcessing = false;

    QString m_ttsBuffer;

    QStringList m_ttsQueue;
    QStringList m_pendingTtsSentences;

    QString m_ttsVoice = QStringLiteral("af_bella");
    QStringList m_ttsVoices;
};

#endif // CHATBACKEND_H