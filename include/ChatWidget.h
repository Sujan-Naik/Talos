#ifndef CHATWIDGET_H
#define CHATWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QRect>
#include <QTimer>
#include <QWebEngineView>

#include <vector>

class InferenceService;
class AudioRecorder;
class CaptureOverlay;
class ChatBackend;

class ChatWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChatWidget(
        InferenceService *inferenceService,
        QWidget *parent = nullptr
    );

    ~ChatWidget() override = default;

    void setHoleRect(
        const QRect &hole
    );

    void setHoleRect(
        const QRect &hole,
        bool enabled
    );

    void setHoleEnabled(
        bool enabled
    );

    ChatBackend *backend() const
    {
        return m_backend;
    }

    QWebEngineView *webView() const
    {
        return m_webEngineView;
    }

signals:
    void messageSent(
        const QString &text
    );

protected:
    void resizeEvent(
        QResizeEvent *event
    ) override;

    void paintEvent(
        QPaintEvent *event
    ) override;

private slots:
    void captureAndSetText();
    void toggleMicrophone();
    void processVadChunk();
    void stopMicrophoneAndTranscribe();

    void handleLlmDelta(
        const QString &deltaText
    );

    void handleLlmFinished();

    void handleLlmError(
        const QString &error
    );

private:
    void appendMessageAsUser(
        const QString &text
    );

    void appendMessageAsAi(
        const QString &text
    );

    void sendApiRequest();

    void appendMessage(
        const QString &text,
        bool isUser
    );

    void appendToCurrentAiMessage(
        const QString &deltaText
    );

    void syncHoleToJavaScript();
    void updateClippingMask();

private:
    InferenceService *m_inference =
        nullptr;

    QWebEngineView *m_webEngineView =
        nullptr;

    AudioRecorder *m_recorder =
        nullptr;

    QTimer *m_vadTimer =
        nullptr;

    CaptureOverlay *m_overlay =
        nullptr;

    ChatBackend *m_backend =
        nullptr;

    QJsonArray m_conversationHistory;

    QRect m_holeRect;
    QRect m_previousHoleRect;

    bool m_isPageLoaded = false;
    bool m_isRecording = false;
    bool m_hasSpeechStarted = false;
    bool m_isStreamingAi = false;
    bool m_holeEnabled = true;

    int m_silenceMs = 0;
};

#endif // CHATWIDGET_H