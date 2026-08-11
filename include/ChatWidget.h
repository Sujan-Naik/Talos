#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QPushButton>
#include <QWebEngineView>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <QRect>
#include <QString>

#include "CaptureOverlay.h"
#include "AudioRecorder.h"
#include "WhisperTranscriber.h"

class ChatWidget : public QWidget {
Q_OBJECT

public:
    explicit ChatWidget(QWidget *parent = nullptr);
    void setHoleRect(const QRect &hole);
    void appendMessageAsUser(const QString &text);
    void appendMessageAsAi(const QString &text);
    void sendApiRequest();

signals:
    void messageSent(const QString &text);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void toggleMicrophone();
    void processVadChunk();
    void stopMicrophoneAndTranscribe();
    void captureAndSetText();
    void handleReadyRead();
    void handleReplyFinished();

private:
    void loadExternalStyleSheet();
    QString getInitialHtml() const;
    void updateSubWidgetLayout();
    void appendMessage(const QString &text, bool isUser);
    void appendToCurrentAiMessage(const QString &deltaText);

    QWebEngineView *m_webEngineView = nullptr;
    QTextEdit *m_inputBox = nullptr;
    QPushButton *m_captureButton = nullptr;
    QPushButton *m_micButton = nullptr;
    QPushButton *m_sendButton = nullptr;

    QRect m_holeRect;
    QRect m_previousHoleRect;
    void updateClippingMask();
    bool m_isPageLoaded = false;

    AudioRecorder *m_recorder = nullptr;
    WhisperTranscriber *m_transcriber = nullptr;
    QTimer *m_vadTimer = nullptr;
    bool m_isRecording = false;
    bool m_hasSpeechStarted = false;
    int m_silenceMs = 0;

    QNetworkAccessManager *m_networkManager = nullptr;
    QNetworkReply *m_currentReply = nullptr;
    QJsonArray m_conversationHistory;
    QByteArray m_streamBuffer;
    bool m_isStreamingAi = false;

    CaptureOverlay *m_overlay = nullptr;
};