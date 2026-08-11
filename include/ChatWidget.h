#pragma once

#include <QWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include "AudioRecorder.h"
#include "WhisperTranscriber.h"
#include "CaptureOverlay.h"


class ChatWidget : public QWidget {
Q_OBJECT
public:
    explicit ChatWidget(QWidget *parent = nullptr);
    void captureAndSetText();

signals:
    void messageSent(const QString &text);

private slots:
    void toggleMicrophone();
    void handleReadyRead();
    void handleReplyFinished();

private:
    void appendMessage(const QString &text, bool isUser);
    void appendToCurrentAiMessage(const QString &deltaText);
    void sendApiRequest();

    QListWidget *m_listWidget = nullptr;
    QLineEdit *m_inputBox = nullptr;
    QPushButton *m_captureButton = nullptr;
    QPushButton *m_micButton = nullptr;
    QPushButton *m_sendButton = nullptr;

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QListWidgetItem *m_currentAiItem = nullptr;
    QByteArray m_streamBuffer;
    QJsonArray m_conversationHistory;

    CaptureOverlay *m_overlay = nullptr;

    AudioRecorder *m_recorder = nullptr;
    WhisperTranscriber *m_transcriber = nullptr;
    bool m_isRecording = false;
};