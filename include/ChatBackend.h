#ifndef CHATBACKEND_H
#define CHATBACKEND_H

#include <QObject>
#include <QString>

class ChatBackend : public QObject {
    Q_OBJECT

public:
    explicit ChatBackend(QObject *parent = nullptr);

    Q_INVOKABLE void onUserSendMessage(const QString &text);
    Q_INVOKABLE void sendMessage(const QString &text);
    Q_INVOKABLE void requestCapture();
    Q_INVOKABLE void requestToggleMic();

    signals:
        void messageReceived(const QString &message);
    void appendUserMessage(const QString &text);
    void appendBotMessage(const QString &text);
    void captureRequested();
    void micToggleRequested();
};

#endif // CHATBACKEND_H