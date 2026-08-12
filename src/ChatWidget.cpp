#include "ChatWidget.h"

// Required for the working screen capture and OCR logic
#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <QScreen>
#include <QGuiApplication>
#include <QThread>
#include <QImage>
#include <QPixmap>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QRegion>
#include <QKeyEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QNetworkRequest>
#include <QWebEnginePage>

ChatWidget::ChatWidget(QWidget *parent)
    : QWidget(parent)
{
    m_webEngineView = new QWebEngineView(this);
    m_inputBox = new QTextEdit(this);
    m_captureButton = new QPushButton(this);
    m_micButton = new QPushButton(this);
    m_sendButton = new QPushButton(this);

    m_inputBox->installEventFilter(this);

    m_recorder = new AudioRecorder(this);
    m_vadTimer = new QTimer(this);

    m_networkManager = new QNetworkAccessManager(this);

    m_captureButton->setText("Capture");
    m_micButton->setText("Mic");
    m_sendButton->setText("Send");

    // Initialize the overlay for the visual scanning effect
    m_overlay = new CaptureOverlay(this);

    connect(m_captureButton, &QPushButton::clicked, this, &ChatWidget::captureAndSetText);
    connect(m_micButton, &QPushButton::clicked, this, &ChatWidget::toggleMicrophone);
    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        QString text = m_inputBox->toPlainText().trimmed();
        if (!text.isEmpty()) {
            appendMessageAsUser(text);
            m_inputBox->clear();
            sendApiRequest();
        }
    });

    connect(m_vadTimer, &QTimer::timeout, this, &ChatWidget::processVadChunk);

    connect(m_webEngineView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_isPageLoaded = ok;
        if (m_isPageLoaded) {
            syncHoleToJavaScript();
        }
    });

    QString initialHtml = getInitialHtml();
    QString appDir = QCoreApplication::applicationDirPath();
    m_webEngineView->setHtml(initialHtml, QUrl::fromLocalFile(appDir + "/"));

    loadExternalStyleSheet();
    updateSubWidgetLayout();
}

void ChatWidget::setHoleRect(const QRect &hole) {
    if (m_holeRect == hole) return;

    m_previousHoleRect = m_holeRect;
    m_holeRect = hole;

    updateClippingMask();

    QRect regionToUpdate = m_previousHoleRect.united(m_holeRect);
    if (regionToUpdate.isEmpty()) {
        update();
    } else {
        update(regionToUpdate);
    }

    syncHoleToJavaScript();
}

void ChatWidget::appendMessageAsUser(const QString &text) {
    appendMessage(text, true);
    QJsonObject messageObj;
    messageObj["role"] = "user";
    messageObj["content"] = text;
    m_conversationHistory.append(messageObj);
    emit messageSent(text);
}

void ChatWidget::appendMessageAsAi(const QString &text) {
    appendMessage(text, false);
    QJsonObject messageObj;
    messageObj["role"] = "assistant";
    messageObj["content"] = text;
    m_conversationHistory.append(messageObj);
}

void ChatWidget::sendApiRequest() {
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    QUrl url("http://127.0.0.1:8080/v1/chat/completions");
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject json;
    json["model"] = "local-model";
    json["messages"] = m_conversationHistory;
    json["stream"] = true;

    QJsonDocument doc(json);
    m_streamBuffer.clear();
    m_isStreamingAi = false;

    m_currentReply = m_networkManager->post(request, doc.toJson());
    connect(m_currentReply, &QNetworkReply::readyRead, this, &ChatWidget::handleReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &ChatWidget::handleReplyFinished);
}

void ChatWidget::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateSubWidgetLayout();
    updateClippingMask();
    syncHoleToJavaScript();
}

void ChatWidget::paintEvent(QPaintEvent *event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (!m_holeRect.isEmpty()) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(m_holeRect, Qt::transparent);
    }

    QWidget::paintEvent(event);
}

bool ChatWidget::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_inputBox && event->type() == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            if (!(keyEvent->modifiers() & Qt::ShiftModifier)) {
                m_sendButton->click();
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatWidget::toggleMicrophone() {
    if (!m_isRecording) {
        m_isRecording = true;
        m_hasSpeechStarted = false;
        m_silenceMs = 0;
        m_micButton->setText("Stop");
        m_recorder->startRecording();
        m_vadTimer->start(100);
    } else {
        stopMicrophoneAndTranscribe();
    }
}

void ChatWidget::processVadChunk() {
    if (!m_isRecording) return;

    std::vector<float> recentSamples = m_recorder->getRecentSamples(1600);
    if (recentSamples.empty()) return;

    float energy = 0.0f;
    for (float sample : recentSamples) {
        energy += sample * sample;
    }
    energy /= static_cast<float>(recentSamples.size());

    bool isSpeech = (energy > 0.001f);

    if (isSpeech) {
        m_hasSpeechStarted = true;
        m_silenceMs = 0;
    } else if (m_hasSpeechStarted) {
        m_silenceMs += 100;
        if (m_silenceMs >= 1500) {
            stopMicrophoneAndTranscribe();
        }
    }
}

void ChatWidget::stopMicrophoneAndTranscribe() {
    m_vadTimer->stop();
    m_isRecording = false;
    m_micButton->setText("Mic");

    std::vector<float> pcmData = m_recorder->stopRecording();
    if (!pcmData.empty()) {
        QString transcribedText = m_transcriber->transcribe(pcmData);
        if (!transcribedText.isEmpty()) {
            m_inputBox->setText(transcribedText);
        }
    }
}

// -------------------------------------------------------------------------
// Working Capture & OCR Logic Restored
// -------------------------------------------------------------------------

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

    // Map the hole rectangle to global screen coordinates
    QRect holeRect = m_holeRect.isValid() && !m_holeRect.isEmpty() ? m_holeRect : QRect(200, 150, 400, 300);
    QPoint globalTopLeft = this->mapToGlobal(holeRect.topLeft());
    QRect globalCaptureRect(globalTopLeft.x(), globalTopLeft.y(), holeRect.width(), holeRect.height());

    // 1. Hide the window so it's not in the screenshot
    topWindow->hide();
    qApp->processEvents();

    // Wait slightly to give the OS compositor time to remove the window
    QThread::msleep(250);

    QScreen *targetScreen = QGuiApplication::screenAt(globalCaptureRect.center());
    if (!targetScreen) {
        targetScreen = QGuiApplication::primaryScreen();
    }

    qreal dpr = targetScreen->devicePixelRatio();
    QRect screenGeo = targetScreen->geometry();

    // Use exact floating-point calculations before rounding to avoid vertical drift
    int cropX = std::round((globalCaptureRect.x() - screenGeo.x()) * dpr);
    int cropY = std::round((globalCaptureRect.y() - screenGeo.y()) * dpr);
    int cropW = std::round(globalCaptureRect.width() * dpr);
    int cropH = std::round(globalCaptureRect.height() * dpr);

    QPixmap fullScreen = targetScreen->grabWindow(0);
    QPixmap screenshot;

    if (!fullScreen.isNull()) {
        QRect nativeCropRect(cropX, cropY, cropW, cropH);
        screenshot = fullScreen.copy(nativeCropRect.intersected(fullScreen.rect()));
    }

    // 3. Restore the window
    topWindow->show();
    topWindow->raise();
    topWindow->activateWindow();
    qApp->processEvents();

    if (m_overlay) {
        m_overlay->startScan(globalCaptureRect);
        qApp->processEvents();
    }

    // 4. Perform direct inline OCR using your working logic
    QString extractedText;
    if (!screenshot.isNull()) {
        extractedText = performScreenOCR(screenshot.toImage());
    }

    if (m_overlay) {
        m_overlay->stopScan();
    }

    if (!extractedText.isEmpty()) {
        QString currentText = m_inputBox->toPlainText();
        m_inputBox->setPlainText(currentText + (currentText.isEmpty() ? "" : "\n") + extractedText);
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
        m_captureButton->setText("Capture");
        m_captureButton->setObjectName("");
        m_captureButton->setStyle(m_captureButton->style());
    });
}

// -------------------------------------------------------------------------

void ChatWidget::handleReadyRead() {
    if (!m_currentReply) return;

    m_streamBuffer.append(m_currentReply->readAll());

    while (m_streamBuffer.contains('\n')) {
        int newlineIdx = m_streamBuffer.indexOf('\n');
        QByteArray line = m_streamBuffer.left(newlineIdx).trimmed();
        m_streamBuffer.remove(0, newlineIdx + 1);

        if (line.isEmpty()) continue;

        if (line.startsWith("data: ")) {
            line = line.mid(6).trimmed();
        }

        if (line == "[DONE]") {
            break;
        }

        QJsonDocument doc = QJsonDocument::fromJson(line);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject root = doc.object();
            QString deltaText;

            if (root.contains("choices")) {
                QJsonArray choices = root["choices"].toArray();
                if (!choices.isEmpty()) {
                    QJsonObject choice = choices[0].toObject();
                    if (choice.contains("delta")) {
                        QJsonObject delta = choice["delta"].toObject();
                        if (delta.contains("content")) {
                            deltaText = delta["content"].toString();
                        }
                    } else if (choice.contains("text")) {
                        deltaText = choice["text"].toString();
                    }
                }
            }
            else if (root.contains("response")) {
                deltaText = root["response"].toString();
            } else if (root.contains("message")) {
                QJsonObject msg = root["message"].toObject();
                if (msg.contains("content")) {
                    deltaText = msg["content"].toString();
                }
            }

            if (!deltaText.isEmpty()) {
                if (!m_isStreamingAi) {
                    m_isStreamingAi = true;
                    appendMessageAsAi(deltaText);
                } else {
                    appendToCurrentAiMessage(deltaText);
                    if (!m_conversationHistory.isEmpty()) {
                        QJsonObject lastMsg = m_conversationHistory.last().toObject();
                        if (lastMsg["role"] == "assistant") {
                            lastMsg["content"] = lastMsg["content"].toString() + deltaText;
                            m_conversationHistory[m_conversationHistory.size() - 1] = lastMsg;
                        }
                    }
                }
            }
        }
    }
}

void ChatWidget::handleReplyFinished() {
    if (m_currentReply) {
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }
    m_isStreamingAi = false;
}

void ChatWidget::loadExternalStyleSheet() {
    QString appDir = QCoreApplication::applicationDirPath();
    QFile cssFile(appDir + "/style.css");
    if (cssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString style = QString::fromUtf8(cssFile.readAll());
        this->setStyleSheet(style);
        cssFile.close();
    }
}

QString ChatWidget::getInitialHtml() const {
    QString appDir = QCoreApplication::applicationDirPath();
    QFile htmlFile(appDir + "/chat.html");
    if (htmlFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString content = QString::fromUtf8(htmlFile.readAll());
        htmlFile.close();
        return content;
    }
    return QString();
}

void ChatWidget::updateSubWidgetLayout() {
    int w = width();
    int h = height();
    int controlsHeight = 100;
    int buttonWidth = 75;
    int buttonHeight = 32;
    int spacing = 8;
    int margin = 10;

    m_webEngineView->setGeometry(0, 0, w, h - controlsHeight);

    int inputY = h - controlsHeight + margin;
    int inputWidth = w - (buttonWidth + margin * 3);
    int inputHeight = controlsHeight - (margin * 2);

    m_inputBox->setGeometry(margin, inputY, inputWidth, inputHeight);

    int btnX = margin + inputWidth + margin;
    m_captureButton->setGeometry(btnX, inputY, buttonWidth, buttonHeight);
    m_micButton->setGeometry(btnX, inputY + buttonHeight + spacing / 2, buttonWidth, buttonHeight);
    m_sendButton->setGeometry(btnX, inputY + (buttonHeight + spacing / 2) * 2, buttonWidth, buttonHeight);
}

void ChatWidget::appendMessage(const QString &text, bool isUser) {
    if (!m_isPageLoaded) {
        qWarning() << "[ChatWidget] Page not loaded yet! Delaying message.";
        return;
    }

    QString escapedText = text;
    escapedText.replace("\\", "\\\\");
    escapedText.replace("\"", "\\\"");
    escapedText.replace("\n", "\\n");
    escapedText.replace("\r", "");

    QString js = QString("if (typeof window.appendMessage === 'function') window.appendMessage(\"%1\", %2);")
                     .arg(escapedText)
                     .arg(isUser ? "true" : "false");

    m_webEngineView->page()->runJavaScript(js);
}

void ChatWidget::appendToCurrentAiMessage(const QString &deltaText) {
    if (!m_isPageLoaded) return;

    QString escapedText = deltaText;
    escapedText.replace("\\", "\\\\");
    escapedText.replace("\"", "\\\"");
    escapedText.replace("\n", "\\n");
    escapedText.replace("\r", "");

    QString js = QString("if (typeof window.appendToLastAiMessage === 'function') window.appendToLastAiMessage(\"%1\");")
                     .arg(escapedText);

    m_webEngineView->page()->runJavaScript(js);
}

void ChatWidget::syncHoleToJavaScript() {
    if (!m_isPageLoaded || !m_webEngineView) return;

    QRect localHole = m_holeRect.intersected(m_webEngineView->geometry());
    QPoint relativePos = m_webEngineView->mapFromParent(localHole.topLeft());

    int x = relativePos.x();
    int y = relativePos.y();
    int w = localHole.width();
    int h = localHole.height();
    int viewW = m_webEngineView->width();

    QString js = QString("if (typeof window.updateHoleRect === 'function') window.updateHoleRect(%1, %2, %3, %4, %5);")
                     .arg(x)
                     .arg(y)
                     .arg(w)
                     .arg(h)
                     .arg(viewW);

    m_webEngineView->page()->runJavaScript(js);
}

void ChatWidget::updateClippingMask() {
    QRegion maskRegion(rect());
    if (!m_holeRect.isEmpty()) {
        maskRegion = maskRegion.subtracted(QRegion(m_holeRect));
    }
    setMask(maskRegion);
}