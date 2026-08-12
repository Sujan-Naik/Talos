#include "ChatBackend.h"
#include <QDebug>

ChatBackend::ChatBackend(QObject *parent)
    : QObject(parent) {
    qDebug() << "[ChatBackend] Constructor initialized successfully.";
}

void ChatBackend::onUserSendMessage(const QString &text) {
    qDebug() << "[ChatBackend] onUserSendMessage triggered with text:" << text;
    emit messageReceived(text);
}

void ChatBackend::sendMessage(const QString &text) {
    qDebug() << "[ChatBackend] sendMessage triggered with text:" << text;
    onUserSendMessage(text);
}

void ChatBackend::requestCapture() {
    qDebug() << "[ChatBackend] requestCapture triggered.";
    emit captureRequested();
}

void ChatBackend::requestToggleMic() {
    qDebug() << "[ChatBackend] requestToggleMic triggered.";
    emit micToggleRequested();
}