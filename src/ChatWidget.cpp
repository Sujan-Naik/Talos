#include "../include/ChatWidget.h"

#include "../include/AudioRecorder.h"
#include "../include/CaptureOverlay.h"
#include "../include/ChatBackend.h"
#include "../include/InferenceService.h"

#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QScreen>
#include <QThread>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QDebug>
#include <QFuture>
#include <QFutureWatcher>

#include <QtConcurrent/QtConcurrent>

#include <cmath>


ChatWidget::ChatWidget(
    InferenceService *inferenceService,
    QWidget *parent
)
    : QWidget(parent)
    , m_inference(inferenceService)
{
    auto *layout =
        new QVBoxLayout(
            this
        );

    layout->setContentsMargins(
        0,
        0,
        0,
        0
    );

    m_webEngineView =
        new QWebEngineView(
            this
        );

    layout->addWidget(
        m_webEngineView
    );

    m_recorder =
        new AudioRecorder(
            this
        );

    m_vadTimer =
        new QTimer(
            this
        );

    m_backend =
        new ChatBackend(
            m_inference,
            this
        );

    m_overlay =
        new CaptureOverlay(
            this
        );

    auto *channel =
        new QWebChannel(
            m_webEngineView->page()
        );

    channel->registerObject(
        QStringLiteral(
            "backend"
        ),
        m_backend
    );

    m_webEngineView
        ->page()
        ->setWebChannel(
            channel
        );

    connect(
        m_backend,
        &ChatBackend::messageReceived,
        this,
        [this](const QString &text) {

            if (text.isEmpty())
                return;

            appendMessageAsUser(
                text
            );

            sendApiRequest();
        }
    );

    connect(
        m_backend,
        &ChatBackend::captureRequested,
        this,
        &ChatWidget::captureAndSetText
    );

    connect(
        m_backend,
        &ChatBackend::micToggleRequested,
        this,
        &ChatWidget::toggleMicrophone
    );

    connect(
        m_vadTimer,
        &QTimer::timeout,
        this,
        &ChatWidget::processVadChunk
    );

    if (m_inference) {

        connect(
            m_inference,
            &InferenceService::llmDelta,
            this,
            &ChatWidget::handleLlmDelta
        );

        connect(
            m_inference,
            &InferenceService::llmFinished,
            this,
            &ChatWidget::handleLlmFinished
        );

        connect(
            m_inference,
            &InferenceService::llmError,
            this,
            &ChatWidget::handleLlmError
        );
    }

    connect(
        m_webEngineView,
        &QWebEngineView::loadFinished,
        this,
        [this](bool ok) {

            m_isPageLoaded =
                ok;

            qDebug()
                << "[ChatWidget] Web page load status:"
                << ok;

            if (m_isPageLoaded)
                syncHoleToJavaScript();
        }
    );

    m_webEngineView->load(
        QUrl(
            QStringLiteral(
                "qrc:///widgets/chat/chat.html"
            )
        )
    );

    setHoleEnabled(
        false
    );
}


void ChatWidget::setHoleRect(
    const QRect &hole,
    bool enabled
)
{
    Q_UNUSED(enabled);

    setHoleRect(
        hole
    );
}


void ChatWidget::setHoleRect(
    const QRect &hole
)
{
    QWidget *topWindow =
        window();

    QRect localHole =
        hole;

    if (
        topWindow &&
        topWindow != this
    ) {
        const QPoint localTopLeft =
            mapFrom(
                topWindow,
                hole.topLeft()
            );

        localHole =
            QRect(
                localTopLeft,
                hole.size()
            );
    }

    if (m_holeRect == localHole)
        return;

    m_previousHoleRect =
        m_holeRect;

    m_holeRect =
        localHole;

    updateClippingMask();

    const QRect regionToUpdate =
        m_previousHoleRect.united(
            m_holeRect
        );

    if (regionToUpdate.isEmpty())
        update();
    else
        update(
            regionToUpdate
        );

    syncHoleToJavaScript();
}


void ChatWidget::setHoleEnabled(
    bool enabled
)
{
    m_holeEnabled =
        enabled;

    syncHoleToJavaScript();
    updateClippingMask();
    update();
}


void ChatWidget::syncHoleToJavaScript()
{
    if (
        !m_isPageLoaded ||
        !m_webEngineView
    ) {
        return;
    }

    if (
        !m_holeEnabled ||
        m_holeRect.isEmpty()
    ) {
        m_webEngineView
            ->page()
            ->runJavaScript(
                QStringLiteral(
                    "if(typeof window.updateHoleRect==='function') "
                    "window.updateHoleRect(0,0,0,0,0,0);"
                )
            );

        return;
    }

    const QPoint holeTopLeftInWebView =
        m_webEngineView->mapFrom(
            this,
            m_holeRect.topLeft()
        );

    const QRect holeInWebView(
        holeTopLeftInWebView,
        m_holeRect.size()
    );

    const int viewWidth =
        m_webEngineView->width();

    const int viewHeight =
        m_webEngineView->height();

    const QString script =
        QString(
            QStringLiteral(
                "if(typeof window.updateHoleRect==='function') "
                "window.updateHoleRect(%1,%2,%3,%4,%5,%6);"
            )
        )
        .arg(
            holeInWebView.x()
        )
        .arg(
            holeInWebView.y()
        )
        .arg(
            holeInWebView.width()
        )
        .arg(
            holeInWebView.height()
        )
        .arg(
            viewWidth
        )
        .arg(
            viewHeight
        );

    m_webEngineView
        ->page()
        ->runJavaScript(
            script
        );
}


void ChatWidget::updateClippingMask()
{
    if (
        !m_holeEnabled ||
        m_holeRect.isEmpty()
    ) {
        clearMask();
        return;
    }

    QRegion region(
        rect()
    );

    region =
        region.subtracted(
            QRegion(
                m_holeRect
            )
        );

    setMask(
        region
    );
}


void ChatWidget::appendMessage(
    const QString &text,
    bool isUser
)
{
    if (
        !m_isPageLoaded ||
        !m_webEngineView
    ) {
        return;
    }

    QString escaped =
        text;

    escaped
        .replace(
            "\\",
            "\\\\"
        )
        .replace(
            "\"",
            "\\\""
        )
        .replace(
            "\n",
            "\\n"
        )
        .replace(
            "\r",
            ""
        );

    const QString script =
        QString(
            QStringLiteral(
                "if(typeof window.appendMessage==='function') "
                "window.appendMessage(\"%1\", %2);"
            )
        )
        .arg(
            escaped
        )
        .arg(
            isUser
                ? QStringLiteral("true")
                : QStringLiteral("false")
        );

    m_webEngineView
        ->page()
        ->runJavaScript(
            script
        );
}


void ChatWidget::appendToCurrentAiMessage(
    const QString &deltaText
)
{
    if (
        !m_isPageLoaded ||
        !m_webEngineView
    ) {
        return;
    }

    QString escaped =
        deltaText;

    escaped
        .replace(
            "\\",
            "\\\\"
        )
        .replace(
            "\"",
            "\\\""
        )
        .replace(
            "\n",
            "\\n"
        )
        .replace(
            "\r",
            ""
        );

    const QString script =
        QString(
            QStringLiteral(
                "if(typeof window.appendToLastAiMessage==='function') "
                "window.appendToLastAiMessage(\"%1\");"
            )
        )
        .arg(
            escaped
        );

    m_webEngineView
        ->page()
        ->runJavaScript(
            script
        );
}


void ChatWidget::appendMessageAsUser(
    const QString &text
)
{
    appendMessage(
        text,
        true
    );

    QJsonObject messageObj;

    messageObj["role"] =
        QStringLiteral(
            "user"
        );

    messageObj["content"] =
        text;

    m_conversationHistory.append(
        messageObj
    );

    emit messageSent(
        text
    );
}


void ChatWidget::appendMessageAsAi(
    const QString &text
)
{
    appendMessage(
        text,
        false
    );

    QJsonObject messageObj;

    messageObj["role"] =
        QStringLiteral(
            "assistant"
        );

    messageObj["content"] =
        text;

    m_conversationHistory.append(
        messageObj
    );
}


void ChatWidget::sendApiRequest()
{
    if (m_backend)
        m_backend->stopSpeech();

    if (!m_inference) {

        qWarning()
            << "[ChatWidget] No InferenceService.";

        return;
    }

    if (!m_inference->isLlmReady()) {

        /*
         * IMPORTANT:
         *
         * Do not update Capture state here.
         *
         * Capture is an OCR feature and must remain
         * independent from LLM availability.
         */
        qWarning()
            << "[ChatWidget] LLM unavailable; "
               "cannot send chat request.";

        return;
    }

    m_isStreamingAi =
        false;

    m_inference->sendChatRequest(
        m_conversationHistory
    );
}


void ChatWidget::handleLlmDelta(
    const QString &deltaText
)
{
    if (deltaText.isEmpty())
        return;

    if (m_backend) {
        m_backend->handleAiStreamDelta(
            deltaText
        );
    }

    if (!m_isStreamingAi) {

        m_isStreamingAi =
            true;

        appendMessageAsAi(
            deltaText
        );

        return;
    }

    if (
        !m_conversationHistory.isEmpty() &&
        m_conversationHistory
                .last()
                .toObject()
                .value(
                    QStringLiteral(
                        "role"
                    )
                )
                .toString() ==
            QStringLiteral(
                "assistant"
            )
    ) {
        QJsonObject lastObj =
            m_conversationHistory
                .last()
                .toObject();

        lastObj["content"] =
            lastObj
                .value(
                    QStringLiteral(
                        "content"
                    )
                )
                .toString()
            + deltaText;

        m_conversationHistory[
            m_conversationHistory.size() - 1
        ] =
            lastObj;
    }

    appendToCurrentAiMessage(
        deltaText
    );
}


void ChatWidget::handleLlmFinished()
{
    if (m_backend)
        m_backend->handleAiStreamFinished();

    m_isStreamingAi =
        false;
}


void ChatWidget::handleLlmError(
    const QString &error
)
{
    qWarning()
        << "[ChatWidget] LLM error:"
        << error;

    if (m_backend)
        m_backend->handleAiStreamFinished();

    m_isStreamingAi =
        false;
}


void ChatWidget::resizeEvent(
    QResizeEvent *event
)
{
    QWidget::resizeEvent(
        event
    );

    if (m_webEngineView)
        m_webEngineView->setGeometry(
            rect()
        );

    updateClippingMask();
    syncHoleToJavaScript();
}


void ChatWidget::paintEvent(
    QPaintEvent *event
)
{
    Q_UNUSED(event);

    QPainter painter(
        this
    );

    painter.setRenderHint(
        QPainter::Antialiasing
    );

    if (!m_holeRect.isEmpty()) {

        painter.setCompositionMode(
            QPainter::CompositionMode_Clear
        );

        painter.fillRect(
            m_holeRect,
            Qt::transparent
        );
    }

    QWidget::paintEvent(
        event
    );
}


void ChatWidget::toggleMicrophone()
{
    if (!m_recorder)
        return;

    if (!m_isRecording) {

        m_isRecording =
            true;

        m_hasSpeechStarted =
            false;

        m_silenceMs =
            0;

        m_recorder->startRecording();

        m_vadTimer->start(
            100
        );

        if (m_isPageLoaded) {
            m_webEngineView
                ->page()
                ->runJavaScript(
                    QStringLiteral(
                        "if(typeof window.setMicState==='function') "
                        "window.setMicState('recording');"
                    )
                );
        }

    } else {

        stopMicrophoneAndTranscribe();
    }
}


void ChatWidget::processVadChunk()
{
    if (!m_isRecording)
        return;

    const std::vector<float> recentSamples =
        m_recorder
            ->getRecentSamples(
                1600
            );

    if (recentSamples.empty())
        return;

    float energy =
        0.0f;

    for (
        const float sample :
        recentSamples
    ) {
        energy +=
            sample * sample;
    }

    energy /=
        static_cast<float>(
            recentSamples.size()
        );

    const bool isSpeech =
        energy > 0.001f;

    if (isSpeech) {

        m_hasSpeechStarted =
            true;

        m_silenceMs =
            0;

    } else if (
        m_hasSpeechStarted
    ) {

        m_silenceMs +=
            100;

        if (m_silenceMs >= 1500)
            stopMicrophoneAndTranscribe();
    }
}


void ChatWidget::stopMicrophoneAndTranscribe()
{
    m_vadTimer->stop();

    m_isRecording =
        false;

    if (m_isPageLoaded) {
        m_webEngineView
            ->page()
            ->runJavaScript(
                QStringLiteral(
                    "if(typeof window.setMicState==='function') "
                    "window.setMicState('idle');"
                )
            );
    }

    const std::vector<float> pcmData =
        m_recorder->stopRecording();

    if (
        pcmData.empty() ||
        !m_inference ||
        !m_inference->isSttReady()
    ) {
        return;
    }

    auto *watcher =
        new QFutureWatcher<QString>(
            this
        );

    connect(
        watcher,
        &QFutureWatcher<QString>::finished,
        this,
        [this, watcher]() {

            const QString transcribedText =
                watcher->result();

            watcher->deleteLater();

            if (
                transcribedText.isEmpty() ||
                !m_isPageLoaded
            ) {
                return;
            }

            QString escaped =
                transcribedText;

            escaped
                .replace(
                    "\\",
                    "\\\\"
                )
                .replace(
                    "\"",
                    "\\\""
                )
                .replace(
                    "\n",
                    "\\n"
                )
                .replace(
                    "\r",
                    ""
                );

            m_webEngineView
                ->page()
                ->runJavaScript(
                    QString(
                        QStringLiteral(
                            "if(typeof window.setInputValue==='function') "
                            "window.setInputValue(\"%1\");"
                        )
                    )
                    .arg(
                        escaped
                    )
                );
        }
    );

    const auto future =
        QtConcurrent::run(
            [this, pcmData]() {
                return m_inference
                    ? m_inference->transcribe(
                          pcmData
                      )
                    : QString();
            }
        );

    watcher->setFuture(
        future
    );
}


void ChatWidget::captureAndSetText()
{
    if (m_isPageLoaded) {
        m_webEngineView
            ->page()
            ->runJavaScript(
                QStringLiteral(
                    "if(typeof window.setCaptureState==='function') "
                    "window.setCaptureState('Reading...', false, '');"
                )
            );
    }

    qApp->processEvents();

    if (m_holeRect.isEmpty()) {

        if (m_isPageLoaded) {
            m_webEngineView
                ->page()
                ->runJavaScript(
                    QStringLiteral(
                        "if(typeof window.setCaptureState==='function') "
                        "window.setCaptureState('No Region', true, 'error');"
                    )
                );
        }

        return;
    }

    const QPoint globalTopLeft =
        mapToGlobal(
            m_holeRect.topLeft()
        );

    const QRect globalCaptureRect(
        globalTopLeft,
        m_holeRect.size()
    );

    QWidget *topWindow =
        window();

    if (!topWindow)
        return;

    topWindow->hide();

    qApp->processEvents();

    QThread::msleep(
        250
    );

    QScreen *targetScreen =
        QGuiApplication::screenAt(
            globalCaptureRect.center()
        );

    if (!targetScreen) {
        targetScreen =
            QGuiApplication::primaryScreen();
    }

    if (!targetScreen) {

        topWindow->show();
        topWindow->raise();
        topWindow->activateWindow();

        if (m_isPageLoaded) {
            m_webEngineView
                ->page()
                ->runJavaScript(
                    QStringLiteral(
                        "if(typeof window.setCaptureState==='function') "
                        "window.setCaptureState('Capture Failed', true, 'error');"
                    )
                );
        }

        return;
    }

    const qreal dpr =
        targetScreen->devicePixelRatio();

    const QRect screenGeometry =
        targetScreen->geometry();

    const int cropX =
        std::round(
            (
                globalCaptureRect.x() -
                screenGeometry.x()
            ) *
            dpr
        );

    const int cropY =
        std::round(
            (
                globalCaptureRect.y() -
                screenGeometry.y()
            ) *
            dpr
        );

    const int cropW =
        std::round(
            globalCaptureRect.width() *
            dpr
        );

    const int cropH =
        std::round(
            globalCaptureRect.height() *
            dpr
        );

    const QPixmap fullScreen =
        targetScreen->grabWindow(
            0
        );

    QPixmap screenshot;

    if (!fullScreen.isNull()) {

        QRect nativeCropRect(
            cropX,
            cropY,
            cropW,
            cropH
        );

        nativeCropRect =
            nativeCropRect.intersected(
                fullScreen.rect()
            );

        if (!nativeCropRect.isEmpty()) {
            screenshot =
                fullScreen.copy(
                    nativeCropRect
                );
        }
    }

    topWindow->show();
    topWindow->raise();
    topWindow->activateWindow();

    qApp->processEvents();

    if (m_overlay) {
        m_overlay->startScan(
            globalCaptureRect
        );

        qApp->processEvents();
    }

    QString extractedText;

    if (
        !screenshot.isNull() &&
        m_inference
    ) {
        extractedText =
            m_inference->extractText(
                screenshot.toImage()
            );
    }

    if (m_overlay)
        m_overlay->stopScan();

    if (!extractedText.isEmpty()) {

        appendMessageAsUser(
            extractedText
        );

        /*
         * OCR succeeded. Capture remains a capture/OCR
         * operation. sendApiRequest() may fail because
         * the LLM is unavailable, but that must not
         * change the Capture state.
         */
        sendApiRequest();

        if (m_isPageLoaded) {
            m_webEngineView
                ->page()
                ->runJavaScript(
                    QStringLiteral(
                        "if(typeof window.setCaptureState==='function') "
                        "window.setCaptureState('Copied!', true, 'success'); "
                        "if(typeof window.clearInput==='function') "
                        "window.clearInput();"
                    )
                );
        }

    } else {

        if (m_isPageLoaded) {
            m_webEngineView
                ->page()
                ->runJavaScript(
                    QStringLiteral(
                        "if(typeof window.setCaptureState==='function') "
                        "window.setCaptureState('No Text Found', true, 'error');"
                    )
                );
        }
    }

    QTimer::singleShot(
        1500,
        this,
        [this]() {
            if (m_isPageLoaded) {
                m_webEngineView
                    ->page()
                    ->runJavaScript(
                        QStringLiteral(
                            "if(typeof window.setCaptureState==='function') "
                            "window.setCaptureState('Capture', true, '');"
                        )
                    );
            }
        }
    );
}