#ifndef CHATBACKEND_H
#define CHATBACKEND_H

#include <QObject>
#include <QString>
#include <QDebug>

class TtsManager;

class ChatBackend : public QObject
{
    Q_OBJECT

public:
    explicit ChatBackend(QObject *parent = nullptr);
    ~ChatBackend();

    Q_INVOKABLE bool initializeTts(const QString &modelDir);
    Q_INVOKABLE bool isTtsReady() const;
    Q_INVOKABLE void speak(const QString &text);
    Q_INVOKABLE void stopSpeech();
    Q_INVOKABLE void onTtsToggled(bool enabled);

    // Existing methods
    Q_INVOKABLE void onUserSendMessage(const QString &message);
    Q_INVOKABLE void requestCapture();
    Q_INVOKABLE void requestToggleMic();

    signals:
        void messageReceived(const QString &message);
    void captureRequested();
    void micToggleRequested();
    void ttsPlaybackFinished();
    void ttsError(const QString &error);

private:
    TtsManager *m_tts;
    bool m_ttsInitialized;
};

#endif // CHATBACKEND_H