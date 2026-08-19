#ifndef CHATWIDGET_H
#define CHATWIDGET_H

#include <QWidget>
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
#include "ChatBackend.h"
#include "WhisperTranscriber.h"
#include "TtsManager.h"

class ChatWidget : public QWidget {
    Q_OBJECT

public:
    explicit ChatWidget(QWidget *parent = nullptr);
    ~ChatWidget() override = default;

    void setHoleRect(const QRect &hole);
    void setHoleRect(const QRect &hole, bool enabled);
    void setHoleEnabled(bool enabled);

    ChatBackend* backend() const { return m_backend; }
    QWebEngineView* webView() const { return m_webEngineView; }

signals:
    void messageSent(const QString &text);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private slots:
    void captureAndSetText();
    void toggleMicrophone();
    void processVadChunk();
    void stopMicrophoneAndTranscribe();
    void handleReadyRead();
    void handleReplyFinished();

private:
    void appendMessageAsUser(const QString &text);
    void appendMessageAsAi(const QString &text);
    void sendApiRequest();
    void updateSubWidgetLayout();
    void appendMessage(const QString &text, bool isUser);
    void appendToCurrentAiMessage(const QString &deltaText);
    void syncHoleToJavaScript();
    void updateClippingMask();

    QWebEngineView *m_webEngineView{nullptr};

    AudioRecorder *m_recorder{nullptr};
    WhisperTranscriber *m_transcriber{nullptr};
    QTimer *m_vadTimer{nullptr};
    CaptureOverlay *m_overlay{nullptr};
    ChatBackend *m_backend{nullptr};
    TtsManager *m_ttsManager{nullptr};

    QNetworkAccessManager *m_networkManager{nullptr};
    QNetworkReply *m_currentReply{nullptr};

    QJsonArray m_conversationHistory;
    QByteArray m_streamBuffer;

    QRect m_holeRect;
    QRect m_previousHoleRect;

    bool m_isPageLoaded{false};
    bool m_isRecording{false};
    bool m_hasSpeechStarted{false};
    bool m_isStreamingAi{false};
    bool m_holeEnabled{true};
    int m_silenceMs{0};
};

#endif // CHATWIDGET_H