#include "CodeBackend.h"

CodeBackend::CodeBackend(QObject *parent)
    : QObject(parent)
{
}

void CodeBackend::onCodeChanged(const QString &code)
{
    emit codeUpdated(code);
}

void CodeBackend::onUserSendMessage(const QString &text)
{
    emit messageReceived(text);
}

void CodeBackend::sendMessage(const QString &text)
{
    onUserSendMessage(text);
}