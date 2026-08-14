#include "../include/CodeWidget.h"

CodeWidget::CodeWidget(QWidget *parent)
    : QWidget(parent)
{
    // m_webEngineView = new QWebEngineView(this);
    // m_inputBox = new QTextEdit(this);
    // m_captureButton = new QPushButton(this);
    // m_micButton = new QPushButton(this);
    // m_sendButton = new QPushButton(this);
    //
    // m_inputBox->installEventFilter(this);
    //
    // m_recorder = new AudioRecorder(this);
    // m_vadTimer = new QTimer(this);
    //
    // m_networkManager = new QNetworkAccessManager(this);
    // m_backend = new ChatBackend(this);
    //
    // m_captureButton->setText("Capture");
    // m_micButton->setText("Mic");
    // m_sendButton->setText("Send");
    //
    // m_overlay = new CaptureOverlay(this);
    //
    // auto *channel = new QWebChannel(m_webEngineView->page());
    // channel->registerObject(QStringLiteral("backend"), m_backend);
    // m_webEngineView->page()->setWebChannel(channel);
    //
    // connect(m_backend, &ChatBackend::messageReceived, this, [this](const QString &text) {
    //     if (!text.isEmpty()) {
    //         appendMessageAsUser(text);
    //         sendApiRequest();
    //     }
    // });
    //
    // connect(m_backend, &ChatBackend::captureRequested, this, &ChatWidget::captureAndSetText);
    // connect(m_backend, &ChatBackend::micToggleRequested, this, &ChatWidget::toggleMicrophone);
    //
    // connect(m_captureButton, &QPushButton::clicked, this, &ChatWidget::captureAndSetText);
    // connect(m_micButton, &QPushButton::clicked, this, &ChatWidget::toggleMicrophone);
    // connect(m_sendButton, &QPushButton::clicked, this, [this]() {
    //     QString text = m_inputBox->toPlainText().trimmed();
    //     if (!text.isEmpty()) {
    //         m_inputBox->clear();
    //         sendApiRequest();
    //     }
    // });
    //
    // connect(m_vadTimer, &QTimer::timeout, this, &ChatWidget::processVadChunk);
    //
    // connect(m_webEngineView, &QWebEngineView::loadFinished, this, [this](bool ok) {
    //     m_isPageLoaded = ok;
    //     qDebug() << "[ChatWidget] Web page load status:" << ok;
    //     if (m_isPageLoaded) {
    //         syncHoleToJavaScript();
    //     }
    // });
    //
    // m_webEngineView->load(QUrl("qrc:///chat.html"));
    // updateSubWidgetLayout();
}