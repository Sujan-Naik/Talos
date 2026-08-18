#include "ChatWidget.h"
#include "ChatBackend.h"

#include <tesseract/baseapi.h>
#include <leptonica/allheaders.h>
#include <QScreen>
#include <QGuiApplication>
#include <QThread>
#include <QImage>
#include <QPixmap>
#include <QVBoxLayout>
#include <QPainter>
#include <QRegion>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QNetworkRequest>
#include <QWebEnginePage>
#include <QWebChannel>
#include <QDebug>

#include "AudioRecorder.h"
#include "CaptureOverlay.h"

ChatWidget::ChatWidget(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    m_webEngineView = new QWebEngineView(this);
    layout->addWidget(m_webEngineView);

    m_recorder = new AudioRecorder(this);
    m_vadTimer = new QTimer(this);
    m_networkManager = new QNetworkAccessManager(this);
    m_backend = new ChatBackend(this);
    m_overlay = new CaptureOverlay(this);

    if (m_backend->isTtsReady()) {
        qDebug() << "[ChatWidget] Kokoro TTS model is ready";
    } else {
        qWarning() << "[ChatWidget] Kokoro TTS model is not ready";
        qWarning() << "Make sure these files exist:";
        qWarning() << "  model.onnx";
        qWarning() << "  tokens.txt";
        qWarning() << "  voices.bin";
        qWarning() << "  espeak-ng-data/";
    }

    auto *channel = new QWebChannel(m_webEngineView->page());
    channel->registerObject(QStringLiteral("backend"), m_backend);
    m_webEngineView->page()->setWebChannel(channel);

    connect(m_backend, &ChatBackend::messageReceived, this, [this](const QString &text) {
        if (!text.isEmpty()) {
            appendMessageAsUser(text);
            sendApiRequest();
        }
    });

    connect(m_backend, &ChatBackend::captureRequested, this, &ChatWidget::captureAndSetText);
    connect(m_backend, &ChatBackend::micToggleRequested, this, &ChatWidget::toggleMicrophone);
    connect(m_vadTimer, &QTimer::timeout, this, &ChatWidget::processVadChunk);

    connect(m_webEngineView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_isPageLoaded = ok;
        qDebug() << "[ChatWidget] Web page load status:" << ok;
        if (m_isPageLoaded) {
            syncHoleToJavaScript();
        }
    });

    m_webEngineView->load(QUrl("qrc:///widgets/chat/chat.html"));
    setHoleEnabled(false);
}

void ChatWidget::setHoleRect(const QRect &hole, bool enabled) {
    Q_UNUSED(enabled);
    setHoleRect(hole);
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

void ChatWidget::setHoleEnabled(bool enabled) {
    m_holeEnabled = enabled;
    syncHoleToJavaScript();
    updateClippingMask();
    update();
}

void ChatWidget::updateSubWidgetLayout() {
    if (m_webEngineView) {
        m_webEngineView->setGeometry(rect());
    }
}

void ChatWidget::syncHoleToJavaScript() {
    if (!m_isPageLoaded || !m_webEngineView) return;

    if (!m_holeEnabled || m_holeRect.isEmpty()) {
        m_webEngineView->page()->runJavaScript("if(typeof window.updateHoleRect==='function') window.updateHoleRect(0, 0, 0, 0, 0);");
        return;
    }

    QString script = QString("if(typeof window.updateHoleRect==='function') window.updateHoleRect(%1, %2, %3, %4, %5);")
                         .arg(m_holeRect.x())
                         .arg(m_holeRect.y())
                         .arg(m_holeRect.width())
                         .arg(m_holeRect.height())
                         .arg(width());
    m_webEngineView->page()->runJavaScript(script);
}

void ChatWidget::updateClippingMask() {
    if (!m_holeEnabled || m_holeRect.isEmpty()) {
        clearMask();
        return;
    }

    QRegion region(rect());
    region = region.subtracted(QRegion(m_holeRect));
    setMask(region);
}

void ChatWidget::appendMessage(const QString &text, bool isUser) {
    if (!m_isPageLoaded || !m_webEngineView) return;

    QString escaped = text;
    escaped.replace("\\", "\\\\")
           .replace("\"", "\\\"")
           .replace("\n", "\\n")
           .replace("\r", "");

    QString script = QString("if(typeof window.appendMessage==='function') window.appendMessage(\"%1\", %2);")
                         .arg(escaped)
                         .arg(isUser ? "true" : "false");
    m_webEngineView->page()->runJavaScript(script);
}

void ChatWidget::appendToCurrentAiMessage(const QString &deltaText) {
    if (!m_isPageLoaded || !m_webEngineView) return;

    QString escaped = deltaText;
    escaped.replace("\\", "\\\\")
           .replace("\"", "\\\"")
           .replace("\n", "\\n")
           .replace("\r", "");

    QString script = QString("if(typeof window.appendToLastAiMessage==='function') window.appendToLastAiMessage(\"%1\");")
                         .arg(escaped);
    m_webEngineView->page()->runJavaScript(script);
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
    if (m_backend) {
        m_backend->stopSpeech();
    }

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

void ChatWidget::toggleMicrophone() {
    if (!m_isRecording) {
        m_isRecording = true;
        m_hasSpeechStarted = false;
        m_silenceMs = 0;
        m_recorder->startRecording();
        m_vadTimer->start(100);

        if (m_isPageLoaded) {
            m_webEngineView->page()->runJavaScript("if(typeof window.setMicState==='function') window.setMicState('recording');");
        }
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

    if (m_isPageLoaded) {
        m_webEngineView->page()->runJavaScript("if(typeof window.setMicState==='function') window.setMicState('idle');");
    }

    std::vector<float> pcmData = m_recorder->stopRecording();
    if (!pcmData.empty() && m_transcriber) {
        QString transcribedText = m_transcriber->transcribe(pcmData);
        if (!transcribedText.isEmpty() && m_isPageLoaded) {
            QString escaped = transcribedText;
            escaped.replace("\\", "\\\\").replace("\"", "\\\"").replace("\n", "\\n").replace("\r", "");

            m_webEngineView->page()->runJavaScript(
                QString("if(typeof window.setInputValue==='function') window.setInputValue(\"%1\");").arg(escaped)
            );
        }
    }
}

static QString performScreenOCR(const QImage &srcImage) {
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
    if (m_isPageLoaded) {
        m_webEngineView->page()->runJavaScript("if(typeof window.setCaptureState==='function') window.setCaptureState('Reading...', false, '');");
    }
    qApp->processEvents();

    QWidget *topWindow = this->window();

    QRect holeRect = m_holeRect.isValid() && !m_holeRect.isEmpty() ? m_holeRect : QRect(200, 150, 400, 300);
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

    topWindow->show();
    topWindow->raise();
    topWindow->activateWindow();
    qApp->processEvents();

    if (m_overlay) {
        m_overlay->startScan(globalCaptureRect);
        qApp->processEvents();
    }

    QString extractedText;
    if (!screenshot.isNull()) {
        extractedText = performScreenOCR(screenshot.toImage());
    }

    if (m_overlay) {
        m_overlay->stopScan();
    }

    if (!extractedText.isEmpty()) {
        appendMessageAsUser(extractedText);
        sendApiRequest();

        if (m_isPageLoaded) {
            m_webEngineView->page()->runJavaScript("if(typeof window.setCaptureState==='function') window.setCaptureState('Copied!', true, 'success'); if(typeof window.clearInput==='function') window.clearInput();");
        }
    } else {
        if (m_isPageLoaded) {
            m_webEngineView->page()->runJavaScript("if(typeof window.setCaptureState==='function') window.setCaptureState('No Text Found', true, 'error');");
        }
    }

    QTimer::singleShot(1500, this, [this]() {
        if (m_isPageLoaded) {
            m_webEngineView->page()->runJavaScript("if(typeof window.setCaptureState==='function') window.setCaptureState('Capture', true, '');");
        }
    });
}

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
                    }
                }
            }

            if (!deltaText.isEmpty()) {
                if (m_backend) {
                    m_backend->handleAiStreamDelta(deltaText);
                }

                if (!m_isStreamingAi) {
                    m_isStreamingAi = true;
                    appendMessageAsAi(deltaText);
                } else {
                    if (!m_conversationHistory.isEmpty() && m_conversationHistory.last().toObject()["role"].toString() == "assistant") {
                        QJsonObject lastObj = m_conversationHistory.last().toObject();
                        lastObj["content"] = lastObj["content"].toString() + deltaText;
                        m_conversationHistory[m_conversationHistory.size() - 1] = lastObj;
                    }

                    appendToCurrentAiMessage(deltaText);
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

    if (m_backend) {
        m_backend->handleAiStreamFinished();
    }
}