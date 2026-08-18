#pragma once

#include <QObject>
#include <QString>

class CodeBackend final : public QObject
{
    Q_OBJECT

public:
    explicit CodeBackend(QObject *parent = nullptr);

    Q_INVOKABLE void onCodeChanged(const QString &code);
    Q_INVOKABLE void onUserSendMessage(const QString &text);
    Q_INVOKABLE void sendMessage(const QString &text);

    Q_INVOKABLE void requestReview(const QString &scope);
    Q_INVOKABLE void requestOpenFile(const QString &relativePath);

    signals:
        void codeUpdated(const QString &code);
    void messageReceived(const QString &text);

    void reviewRequested(const QString &scope);
    void openFileRequested(const QString &relativePath);
};