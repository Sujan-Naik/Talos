#include "../include/ChatWidget.h"
#include "../include/window/MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QUrl>
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <QString>
#include <QImage>
#include <QGuiApplication>
#include <QScreen>
#include <QApplication>
#include <QTimer>
#include <QThread>
#include <QDebug>
#include <cmath>
#include <QFile>
#include <QWebEnginePage>
#include <QKeyEvent>

ChatWidget::ChatWidget(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    loadExternalStyleSheet();

    m_webEngineView = new QWebEngineView(this);
    m_webEngineView->setObjectName("webEngineView");
    m_webEngineView->page()->setBackgroundColor(Qt::transparent);

    connect(m_webEngineView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_isPageLoaded = ok;
        if (ok && m_holeRect.isValid()) {
            setHoleRect(m_holeRect);
        }
    });

    m_webEngineView->setHtml(getInitialHtml());

    m_networkManager = new QNetworkAccessManager(this);
    m_overlay = new CaptureOverlay();

    m_recorder = new AudioRecorder(this);
    m_transcriber = new WhisperTranscriber("ggml-tiny.en.bin", this);

    m_vadTimer = new QTimer(this);
    m_vadTimer->setInterval(100);

    connect(m_vadTimer, &QTimer::timeout, this, &ChatWidget::processVadChunk);

    connect(m_transcriber, &WhisperTranscriber::transcriptionFinished, this, [this](const QString &text) {
        m_micButton->setEnabled(true);
        m_micButton->setText("🎤");
        m_micButton->setObjectName("");
        m_micButton->setStyle(m_micButton->style());
        if (!text.isEmpty()) {
            m_inputBox->setPlainText(text);
            m_inputBox->setFocus();
        }
    });

    m_inputBox = new QTextEdit(this);
    m_inputBox->setPlaceholderText("Ask AI assistant...");
    m_inputBox->setLineWrapMode(QTextEdit::WidgetWidth);
    m_inputBox->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_inputBox->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_captureButton = new QPushButton("Copy Below", this);
    m_micButton = new QPushButton("🎤", this);
    m_sendButton = new QPushButton("Send", this);
    m_sendButton->setObjectName("sendButton");

    connect(m_captureButton, &QPushButton::clicked, this, &ChatWidget::captureAndSetText);
    connect(m_micButton, &QPushButton::clicked, this, &ChatWidget::toggleMicrophone);

    auto sendHandler = [this]() {
        QString text = m_inputBox->toPlainText().trimmed();
        if (!text.isEmpty()) {
            appendMessage(text, true);
            emit messageSent(text);
            sendApiRequest();
            m_inputBox->clear();
        }
    };

    connect(m_sendButton, &QPushButton::clicked, sendHandler);

    m_inputBox->installEventFilter(this);
}

void ChatWidget::loadExternalStyleSheet() {
    QFile styleFile(":/style/ChatWidget.css");
    if (!styleFile.exists()) {
        styleFile.setFileName("ChatWidget.css");
    }
    if (styleFile.open(QFile::ReadOnly | QFile::Text)) {
        QString styleSheet = QLatin1String(styleFile.readAll());
        setStyleSheet(styleSheet);
        styleFile.close();
    }
}

QString ChatWidget::getInitialHtml() const {
    return R"html(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <style>
        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
        }

        body {
            background-color: transparent !important;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
            color: #F4F4F5;
            font-size: 14px;
            padding: 12px;
            overflow-y: auto;
            height: 100vh;
        }

        #obstacle {
            display: none;
            float: left;
            shape-margin: 10px;
            pointer-events: none;
        }

        #chat-container {
            display: flex;
            flex-direction: column;
            gap: 12px;
            width: 100%;
        }

        .message {
            max-width: 85%;
            padding: 10px 14px;
            border-radius: 12px;
            line-height: 1.4;
            word-break: break-word;
            white-space: pre-wrap;
            clear: both;
        }

        .message.user {
            background-color: #007AFF;
            color: #FFFFFF;
            align-self: flex-end;
            border-bottom-right-radius: 2px;
        }

        .message.ai {
            background-color: #323232;
            color: #F4F4F5;
            align-self: flex-start;
            border-bottom-left-radius: 2px;
        }
    </style>
</head>
<body>
    <div id="obstacle"></div>
    <div id="chat-container"></div>

    <script>
        window.chatApp = {
            setObstacle: function(config) {
                const obs = document.getElementById('obstacle');
                if (!config.active) {
                    obs.style.display = 'none';
                    obs.style.width = '0px';
                    obs.style.height = '0px';
                    return;
                }
                obs.style.display = 'block';
                obs.style.width = config.width + 'px';
                obs.style.height = config.height + 'px';
                obs.style.shapeOutside = `rect(0px ${config.width}px ${config.height}px 0px)`;
            },

            addMessage: function(data) {
                const container = document.getElementById('chat-container');
                let div = null;

                if (data.isDelta) {
                    const messages = container.getElementsByClassName('message ai');
                    if (messages.length > 0) {
                        div = messages[messages.length - 1];
                    }
                }

                if (!div) {
                    div = document.createElement('div');
                    div.className = 'message ' + (data.isUser ? 'user' : 'ai');
                    container.appendChild(div);
                }

                div.textContent += data.text;
                window.scrollTo(0, document.body.scrollHeight);
            },

            clear: function() {
                document.getElementById('chat-container').innerHTML = '';
            }
        };
    </script>
</body>
</html>
    )html";
}

void ChatWidget::setHoleRect(const QRect &rect) {
    if (m_holeRect == rect) return;

    m_previousHoleRect = m_holeRect;
    m_holeRect = rect;

    updateClippingMask();

    if (m_isPageLoaded) {
        QJsonObject holeObj;
        holeObj["active"] = m_holeRect.isValid() && !m_holeRect.isEmpty();
        holeObj["width"] = m_holeRect.width();
        holeObj["height"] = m_holeRect.height();

        QString jsonParam = QString::fromUtf8(QJsonDocument(holeObj).toJson(QJsonDocument::Compact));
        m_webEngineView->page()->runJavaScript(QString("window.chatApp.setObstacle(%1);").arg(jsonParam));
    }

    update();
}

void ChatWidget::updateClippingMask() {
    clearMask();

    if (m_holeRect.isValid() && !m_holeRect.isEmpty()) {
        QRegion fullRegion(rect());
        QRegion holeRegion(m_holeRect);
        setMask(fullRegion.subtracted(holeRegion));
    }
}

void ChatWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateSubWidgetLayout();
    updateClippingMask();
}

void ChatWidget::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (m_holeRect.isValid() && !m_holeRect.isEmpty()) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(m_holeRect, Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
}

void ChatWidget::updateSubWidgetLayout() {
    int margin = 10;
    int inputHeight = 40;
    int btnWidth = 85;
    int micWidth = 40;
    int sendWidth = 60;
    int spacing = 6;

    int w = width();
    int h = height();

    int inputY = h - inputHeight - margin;
    int inputX = margin;
    int totalInputWidth = w - (margin * 2);

    int availableInputBoxWidth = totalInputWidth - (btnWidth + micWidth + sendWidth + (spacing * 3));

    m_inputBox->setGeometry(inputX, inputY, availableInputBoxWidth, inputHeight);
    m_captureButton->setGeometry(inputX + availableInputBoxWidth + spacing, inputY, btnWidth, inputHeight);
    m_micButton->setGeometry(inputX + availableInputBoxWidth + btnWidth + (spacing * 2), inputY, micWidth, inputHeight);
    m_sendButton->setGeometry(inputX + availableInputBoxWidth + btnWidth + micWidth + (spacing * 3), inputY, sendWidth, inputHeight);

    int chatY = margin;
    int chatHeight = inputY - (margin * 2);

    m_webEngineView->setGeometry(margin, chatY, w - (margin * 2), chatHeight);
}

bool ChatWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_inputBox && event->type() == QEvent::KeyPress) {
        auto *keyEvent = dynamic_cast<QKeyEvent*>(event);
        if (keyEvent && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
            !(keyEvent->modifiers() & Qt::ShiftModifier)) {
            QString text = m_inputBox->toPlainText().trimmed();
            if (!text.isEmpty()) {
                appendMessage(text, true);
                emit messageSent(text);
                sendApiRequest();
                m_inputBox->clear();
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatWidget::toggleMicrophone() {
    if (!m_isRecording) {
        m_isRecording = true;
        m_hasSpeechStarted = false;
        m_silenceMs = 0;

        m_recorder->startRecording();
        m_vadTimer->start();

        m_micButton->setText("⏹ Listening...");
        m_micButton->setObjectName("micButtonListening");
        m_micButton->setStyle(m_micButton->style());
    } else {
        stopMicrophoneAndTranscribe();
    }
}

void ChatWidget::processVadChunk() {
    if (!m_isRecording) return;

    std::vector<float> recentSamples = m_recorder->getRecentSamples(1600);
    if (recentSamples.empty()) return;

    float sumSquares = 0.0f;
    for (float sample : recentSamples) {
        sumSquares += sample * sample;
    }
    float rms = std::sqrt(sumSquares / static_cast<float>(recentSamples.size()));

    const float speechThreshold = 0.015f;
    const int silenceTimeoutMs = 1200;

    if (rms > speechThreshold) {
        m_hasSpeechStarted = true;
        m_silenceMs = 0;
    } else if (m_hasSpeechStarted) {
        m_silenceMs += m_vadTimer->interval();
        if (m_silenceMs >= silenceTimeoutMs) {
            stopMicrophoneAndTranscribe();
        }
    }
}

void ChatWidget::stopMicrophoneAndTranscribe() {
    if (!m_isRecording) return;

    m_isRecording = false;
    m_vadTimer->stop();

    m_micButton->setEnabled(false);
    m_micButton->setText("Transcribing...");
    m_micButton->setObjectName("");
    m_micButton->setStyle(m_micButton->style());

    std::vector<float> pcmData = m_recorder->stopRecording();
    m_transcriber->transcribeAsync(pcmData);
}

void ChatWidget::appendMessageAsUser(const QString &text) {
    appendMessage(text, true);
}

void ChatWidget::appendMessageAsAi(const QString &text) {
    appendMessage(text, false);
}

void ChatWidget::appendMessage(const QString &text, bool isUser) {
    if (m_isPageLoaded) {
        QJsonObject msgObj;
        msgObj["text"] = text;
        msgObj["isUser"] = isUser;
        msgObj["isDelta"] = false;

        QString jsonParam = QString::fromUtf8(QJsonDocument(msgObj).toJson(QJsonDocument::Compact));
        m_webEngineView->page()->runJavaScript(QString("window.chatApp.addMessage(%1);").arg(jsonParam));
    }

    QJsonObject historyObj;
    historyObj["role"] = isUser ? "user" : "assistant";
    historyObj["content"] = text;
    m_conversationHistory.append(historyObj);
    m_isStreamingAi = !isUser;
}

void ChatWidget::sendApiRequest() {
    m_inputBox->setEnabled(false);
    m_sendButton->setEnabled(false);
    if (m_captureButton) m_captureButton->setEnabled(false);
    if (m_micButton) m_micButton->setEnabled(false);

    m_streamBuffer.clear();

    QNetworkRequest request(QUrl("http://127.0.0.1:8080/v1/chat/completions"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject payload;
    payload["messages"] = m_conversationHistory;
    payload["temperature"] = 0.7;
    payload["stream"] = true;

    QJsonDocument doc(payload);
    m_currentReply = m_networkManager->post(request, doc.toJson());

    connect(m_currentReply, &QNetworkReply::readyRead, this, &ChatWidget::handleReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &ChatWidget::handleReplyFinished);
}

void ChatWidget::handleReadyRead() {
    if (!m_currentReply) return;

    m_streamBuffer.append(m_currentReply->readAll());

    while (m_streamBuffer.contains('\n')) {
        int lineEndIndex = m_streamBuffer.indexOf('\n');
        QByteArray line = m_streamBuffer.left(lineEndIndex).trimmed();
        m_streamBuffer.remove(0, lineEndIndex + 1);

        if (line.startsWith("data: ")) {
            QByteArray jsonData = line.mid(6).trimmed();
            if (jsonData == "[DONE]") continue;

            QJsonDocument doc = QJsonDocument::fromJson(jsonData);
            if (!doc.isNull() && doc.isObject()) {
                QJsonObject rootObj = doc.object();
                QJsonArray choices = rootObj["choices"].toArray();
                if (!choices.isEmpty()) {
                    QJsonObject delta = choices[0].toObject()["delta"].toObject();
                    if (delta.contains("content")) {
                        QString token = delta["content"].toString();
                        appendToCurrentAiMessage(token);
                    }
                }
            }
        }
    }
}

void ChatWidget::appendToCurrentAiMessage(const QString &deltaText) {
    if (!m_isStreamingAi) {
        appendMessage(deltaText, false);
    } else {
        if (m_isPageLoaded) {
            QJsonObject msgObj;
            msgObj["text"] = deltaText;
            msgObj["isUser"] = false;
            msgObj["isDelta"] = true;

            QString jsonParam = QString::fromUtf8(QJsonDocument(msgObj).toJson(QJsonDocument::Compact));
            m_webEngineView->page()->runJavaScript(QString("window.chatApp.addMessage(%1);").arg(jsonParam));
        }

        if (!m_conversationHistory.isEmpty()) {
            QJsonObject lastObj = m_conversationHistory.last().toObject();
            if (lastObj["role"].toString() == "assistant") {
                lastObj["content"] = lastObj["content"].toString() + deltaText;
                m_conversationHistory.last() = lastObj;
            }
        }
    }
}

void ChatWidget::handleReplyFinished() {
    m_inputBox->setEnabled(true);
    m_sendButton->setEnabled(true);
    if (m_captureButton) m_captureButton->setEnabled(true);
    if (m_micButton) m_micButton->setEnabled(true);
    m_isStreamingAi = false;

    if (m_currentReply && m_currentReply->error() != QNetworkReply::NoError) {
        appendMessage(QString("[Error: %1]").arg(m_currentReply->errorString()), false);
    }

    if (m_currentReply) {
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
}

QString performScreenOCR(const QImage &srcImage) {
    if (srcImage.isNull()) return QString();

    QImage processedImage = srcImage.scaled(srcImage.width() * 2, srcImage.height() * 2,
                                            Qt::KeepAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGB888);

    tesseract::TessBaseAPI tess;
    if (tess.Init(NULL, "eng", tesseract::OEM_LSTM_ONLY)) {
        return QString();
    }

    tess.SetPageSegMode(tesseract::PSM_AUTO);
    tess.SetImage(processedImage.bits(), processedImage.width(), processedImage.height(), 3, processedImage.bytesPerLine());

    char *outText = tess.GetUTF8Text();
    QString result = QString::fromUtf8(outText).trimmed();

    delete[] outText;
    tess.End();

    return result;
}

void ChatWidget::captureAndSetText() {
    m_captureButton->setEnabled(false);
    m_captureButton->setText("Reading...");
    qApp->processEvents();

    QWidget *topWindow = this->window();

    auto *mainWindow = qobject_cast<MainWindow*>(topWindow);
    QRect holeRect = mainWindow ? mainWindow->holeRect() : QRect(200, 150, 400, 300);

    QPoint globalTopLeft = topWindow->mapToGlobal(holeRect.topLeft());
    QRect globalCaptureRect(globalTopLeft.x(), globalTopLeft.y(), holeRect.width(), holeRect.height());

    topWindow->hide();
    qApp->processEvents();
    QThread::msleep(250);

    QScreen *targetScreen = QGuiApplication::screenAt(globalCaptureRect.center());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    qreal dpr = targetScreen->devicePixelRatio();

    QRect screenGeo = targetScreen->geometry();
    int cropX = qRound((globalCaptureRect.x() - screenGeo.x()) * dpr);
    int cropY = qRound((globalCaptureRect.y() - screenGeo.y()) * dpr);
    int cropW = qRound(globalCaptureRect.width() * dpr);
    int cropH = qRound(globalCaptureRect.height() * dpr);

    QPixmap fullScreen = targetScreen->grabWindow(0);
    QPixmap screenshot;

    if (!fullScreen.isNull()) {
        QRect nativeCropRect(cropX, cropY, cropW, cropH);
        screenshot = fullScreen.copy(nativeCropRect.intersected(fullScreen.rect()));
    }

    topWindow->show();
    topWindow->raise();
    topWindow->activateWindow();
    qApp->processEvents();

    m_overlay->startScan(globalCaptureRect);
    qApp->processEvents();

    QString extractedText;
    if (!screenshot.isNull()) {
        extractedText = performScreenOCR(screenshot.toImage());
    }

    m_overlay->stopScan();

    if (!extractedText.isEmpty()) {
        QString currentText = m_inputBox->toPlainText();
        m_inputBox->setPlainText(currentText + extractedText);
        m_inputBox->setFocus();

        m_captureButton->setText("Copied!");
        m_captureButton->setObjectName("captureButtonSuccess");
        m_captureButton->setStyle(m_captureButton->style());
    } else {
        m_captureButton->setText("No Text Found");
        m_captureButton->setObjectName("captureButtonError");
        m_captureButton->setStyle(m_captureButton->style());
    }

    QTimer::singleShot(1500, this, [this]() {
        m_captureButton->setEnabled(true);
        m_captureButton->setText("Copy Below");
        m_captureButton->setObjectName("");
        m_captureButton->setStyle(m_captureButton->style());
    });
}