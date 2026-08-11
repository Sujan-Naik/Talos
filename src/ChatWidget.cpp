#include "../include/ChatWidget.h"
#include "../include/window/MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
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
#include <numeric>

ChatWidget::ChatWidget(QWidget *parent) : QWidget(parent) {
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
        m_micButton->setStyleSheet("");
        if (!text.isEmpty()) {
            m_inputBox->setText(text);
            m_inputBox->setFocus();
        }
    });

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);

    m_listWidget = new QListWidget(this);
    m_listWidget->setSelectionMode(QAbstractItemView::NoSelection);
    m_listWidget->setFocusPolicy(Qt::NoFocus);
    m_listWidget->setStyleSheet("QListWidget { background: transparent; border: none; }");

    auto *inputLayout = new QHBoxLayout();
    m_inputBox = new QLineEdit(this);
    m_inputBox->setPlaceholderText("Ask AI assistant...");

    m_captureButton = new QPushButton("Copy Below", this);
    m_micButton = new QPushButton("🎤", this);
    m_sendButton = new QPushButton("Send", this);

    inputLayout->addWidget(m_inputBox);
    inputLayout->addWidget(m_captureButton);
    inputLayout->addWidget(m_micButton);
    inputLayout->addWidget(m_sendButton);

    mainLayout->addWidget(m_listWidget);
    mainLayout->addLayout(inputLayout);

    connect(m_captureButton, &QPushButton::clicked, this, &ChatWidget::captureAndSetText);
    connect(m_micButton, &QPushButton::clicked, this, &ChatWidget::toggleMicrophone);

    auto sendHandler = [this]() {
        QString text = m_inputBox->text().trimmed();
        if (!text.isEmpty()) {
            appendMessage(text, true);
            emit messageSent(text);
            sendApiRequest();
            m_inputBox->clear();
        }
    };

    connect(m_sendButton, &QPushButton::clicked, sendHandler);
    connect(m_inputBox, &QLineEdit::returnPressed, sendHandler);
}

void ChatWidget::toggleMicrophone() {
    if (!m_isRecording) {
        m_isRecording = true;
        m_hasSpeechStarted = false;
        m_silenceMs = 0;

        m_recorder->startRecording();
        m_vadTimer->start();

        m_micButton->setText("⏹ Listening...");
        m_micButton->setStyleSheet("QPushButton { background-color: #c62828; color: white; }");
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
    m_micButton->setStyleSheet("QPushButton { background-color: #555555; color: white; }");

    std::vector<float> pcmData = m_recorder->stopRecording();
    m_transcriber->transcribeAsync(pcmData);
}

void ChatWidget::appendMessageAsUser(const QString &text){
    auto *item = new QListWidgetItem(m_listWidget);
    item->setText(("User: ") + text);
    m_listWidget->addItem(item);
//    m_listWidget->scrollToBottom();

    QJsonObject msgObj;
    msgObj["role"] = "user";
    msgObj["content"] = text;
    m_conversationHistory.append(msgObj);
}


void ChatWidget::appendMessage(const QString &text, bool isUser) {
    auto *item = new QListWidgetItem(m_listWidget);
    item->setText((isUser ? "User: " : "AI: ") + text);
    m_listWidget->addItem(item);
//    m_listWidget->scrollToBottom();

    QJsonObject msgObj;
    msgObj["role"] = isUser ? "user" : "assistant";
    msgObj["content"] = text;
    m_conversationHistory.append(msgObj);
}

void ChatWidget::sendApiRequest() {
    m_inputBox->setEnabled(false);
    m_sendButton->setEnabled(false);
    if (m_captureButton) m_captureButton->setEnabled(false);
    if (m_micButton) m_micButton->setEnabled(false);

    m_currentAiItem = nullptr;
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
    if (!m_currentAiItem) {
        m_currentAiItem = new QListWidgetItem(m_listWidget);
        m_currentAiItem->setText("AI: " + deltaText);
        m_listWidget->addItem(m_currentAiItem);
    } else {
        QString currentText = m_currentAiItem->text();
        m_currentAiItem->setText(currentText + deltaText);
    }
//    m_listWidget->scrollToBottom();
}

void ChatWidget::handleReplyFinished() {
    m_inputBox->setEnabled(true);
    m_sendButton->setEnabled(true);
    if (m_captureButton) m_captureButton->setEnabled(true);
    if (m_micButton) m_micButton->setEnabled(true);

    if (m_currentReply && m_currentReply->error() != QNetworkReply::NoError) {
        appendMessage(QString("[Error: %1]").arg(m_currentReply->errorString()), false);
    } else if (m_currentAiItem) {
        QJsonObject assistantObj;
        assistantObj["role"] = "assistant";
        assistantObj["content"] = m_currentAiItem->text().mid(4);
        m_conversationHistory.append(assistantObj);
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
    m_captureButton->setStyleSheet("QPushButton { background-color: #555555; color: white; }");
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

    qDebug() << "=== COPIED OCR TEXT START ===";
    qDebug().noquote() << extractedText;
    qDebug() << "=== COPIED OCR TEXT END ===";

    if (!extractedText.isEmpty()) {
        m_inputBox->setText(extractedText);
        m_inputBox->setFocus();

        m_captureButton->setText("Copied!");
        m_captureButton->setStyleSheet("QPushButton { background-color: #2e7d32; color: white; }");
    } else {
        m_captureButton->setText("No Text Found");
        m_captureButton->setStyleSheet("QPushButton { background-color: #c62828; color: white; }");
    }

    QTimer::singleShot(1500, this, [this]() {
        m_captureButton->setEnabled(true);
        m_captureButton->setText("Copy Below");
        m_captureButton->setStyleSheet("");
    });
}