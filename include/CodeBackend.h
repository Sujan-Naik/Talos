#ifndef CODEBACKEND_H
#define CODEBACKEND_H

#include <QObject>

class CodeBackend : public QObject
{
    Q_OBJECT
public:
    explicit CodeBackend(QObject *parent = nullptr);

    signals:
        void codeUpdated(const QString &code);
    void messageReceived(const QString &text);

public slots:
    void onCodeChanged(const QString &code);
    void onUserSendMessage(const QString &text);
    void sendMessage(const QString &text);
};

#endif // CODEBACKEND_H