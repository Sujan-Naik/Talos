#include "../include/ChatBackend.h"
#include "../include/TtsManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

ChatBackend::ChatBackend(QObject *parent)
    : QObject(parent)
    , m_tts(nullptr)
    , m_ttsInitialized(false)
{
    qDebug() << "[ChatBackend] Constructor – creating TtsManager...";

    m_tts = new TtsManager(this);

    // Forward useful signals
    connect(m_tts, &TtsManager::playbackFinished, this, &ChatBackend::ttsPlaybackFinished);
    connect(m_tts, &TtsManager::errorOccurred, this, &ChatBackend::ttsError);

    // 1. Try application directory path
    QString modelDir = QCoreApplication::applicationDirPath() + "/models/kokoro-en-v0_19";

    // 2. Fall back to CMake build directory if local binary folder doesn't have it yet
#ifdef KOKORO_MODEL_DIR
    if (!QDir(modelDir).exists()) {
        modelDir = QStringLiteral(KOKORO_MODEL_DIR);
    }
#endif

    if (QDir(modelDir).exists()) {
        if (initializeTts(modelDir)) {
            qDebug() << "[ChatBackend] TTS initialized successfully from" << modelDir;
        } else {
            qWarning() << "[ChatBackend] Failed to initialize TTS from" << modelDir;
        }
    } else {
        qWarning() << "[ChatBackend] Kokoro model dir not found anywhere! Checked:" << modelDir;
        qWarning() << "Make sure CMake successfully downloaded the models or copy them manually.";
    }

    qDebug() << "[ChatBackend] Ready (TtsManager created).";
}

ChatBackend::~ChatBackend()
{
    // Cleanup handled by Qt parent-child relationship
}

bool ChatBackend::initializeTts(const QString &modelDir)
{
    if (m_ttsInitialized) {
        qDebug() << "[ChatBackend] TTS already initialized, skipping";
        return true;
    }

    if (!m_tts) {
        qWarning() << "[ChatBackend] TtsManager not created";
        return false;
    }

    if (!m_tts->initialize(modelDir)) {
        qWarning() << "[ChatBackend] Failed to initialize TTS model";
        return false;
    }

    m_tts->setEnabled(true);
    m_ttsInitialized = true;
    qDebug() << "[ChatBackend] TTS initialized and enabled";
    return true;
}

bool ChatBackend::isTtsReady() const
{
    return m_ttsInitialized && m_tts;
}

void ChatBackend::speak(const QString &text)
{
    if (!m_ttsInitialized || !m_tts) {
        qWarning() << "[ChatBackend] speak() called but TTS model is not initialized";
        emit ttsError("TTS model is not initialized");
        return;
    }

    if (text.trimmed().isEmpty()) {
        return;
    }

    qDebug() << "[ChatBackend] Speaking text:" << text.left(50) << "...";
    m_tts->enqueueSentence(text, 0); // Use default speaker ID 0
}

void ChatBackend::stopSpeech()
{
    if (m_tts) {
        m_tts->stopAndClear();
    }
}

void ChatBackend::onTtsToggled(bool enabled)
{
    if (m_tts) {
        m_tts->setEnabled(enabled);
        qDebug() << "[ChatBackend] TTS toggled:" << (enabled ? "ON" : "OFF");
    }
}

void ChatBackend::onUserSendMessage(const QString &message)
{
    if (!message.trimmed().isEmpty()) {
        emit messageReceived(message);
    }
}

void ChatBackend::requestCapture()
{
    emit captureRequested();
}

void ChatBackend::requestToggleMic()
{
    emit micToggleRequested();
}