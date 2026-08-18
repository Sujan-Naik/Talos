#include "../include/ChatBackend.h"
#include "../include/TtsManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QDebug>
#include <QRegularExpression>
#include <QStringList>

ChatBackend::ChatBackend(QObject *parent)
    : QObject(parent)
    , m_tts(nullptr)
    , m_ttsInitialized(false)
    , m_ttsEnabled(false)
    , m_ttsProcessing(false)
{
    setupTts();
}

ChatBackend::~ChatBackend()
{
    // TtsManager will be cleaned up automatically due to parent hierarchy
}

void ChatBackend::setupTts()
{
    if (m_tts) {
        delete m_tts;
        m_tts = nullptr;
    }

    m_tts = new TtsManager(this);

    connect(m_tts, &TtsManager::sentenceFinished,
            this, &ChatBackend::handleTtsSentenceFinished);
    connect(m_tts, &TtsManager::errorOccurred,
            this, &ChatBackend::handleTtsError);
    connect(m_tts, &TtsManager::serverReady,
            this, &ChatBackend::onTtsServerReady);

    bool ok = m_tts->initialize();  // autoStart = true
    if (!ok) {
        qWarning() << "[ChatBackend] TTS initialisation failed (Docker not available or permission denied).";
        emit ttsError("TTS initialisation failed – Docker not available.");
    }
}

bool ChatBackend::isTtsReady() const
{
    return m_ttsInitialized && m_tts != nullptr;
}

void ChatBackend::onTtsServerReady()
{
    qDebug() << "[ChatBackend] TTS server is ready.";
    m_ttsInitialized = true;

    // Enable TTS by default (you can later toggle via UI)
    m_ttsEnabled = true;
    if (m_tts) {
        m_tts->setEnabled(true);
    }

    // --- TEST: enqueue a test sentence to verify audio ---
    qDebug() << "[ChatBackend] Enqueuing test sentence...";
    m_tts->enqueueSentence("Hello, this is a test of the TTS system.", 0);
    // ------------------------------------------------------

    // Flush any pending sentences that were queued before server ready
    if (!m_pendingTtsSentences.isEmpty()) {
        qDebug() << "[ChatBackend] Flushing" << m_pendingTtsSentences.size() << "pending sentences.";
        for (const QString &sentence : m_pendingTtsSentences) {
            enqueueTtsSentence(sentence);
        }
        m_pendingTtsSentences.clear();
    }

    // If TTS is enabled, start processing the queue
    if (m_ttsEnabled) {
        processTtsQueue();
    }
}

void ChatBackend::speak(const QString &text)
{
    qDebug() << "[ChatBackend] speak() called with text length:" << text.length();
    if (!m_ttsInitialized || !m_tts || !m_ttsEnabled || text.trimmed().isEmpty()) {
        qDebug() << "[ChatBackend] speak aborted (not ready or disabled)";
        return;
    }
    enqueueTtsSentence(text);
}

void ChatBackend::stopSpeech()
{
    qDebug() << "[ChatBackend] stopSpeech called";
    m_ttsBuffer.clear();
    m_ttsQueue.clear();
    m_ttsProcessing = false;
    m_pendingTtsSentences.clear();

    if (m_tts) {
        m_tts->stopAndClear();
    }
}

void ChatBackend::onTtsToggled(bool enabled)
{
    qDebug() << "[ChatBackend] TTS toggled to" << enabled;
    m_ttsEnabled = enabled;

    if (m_tts) {
        m_tts->setEnabled(enabled);
    }

    if (!enabled) {
        stopSpeech();
    } else if (m_ttsInitialized) {
        processTtsQueue();
    }
}

void ChatBackend::onUserSendMessage(const QString &message)
{
    qDebug() << "[ChatBackend] onUserSendMessage";
    stopSpeech();

    if (!message.trimmed().isEmpty()) {
        emit messageReceived(message);
    }
}

void ChatBackend::handleAiStreamDelta(const QString &deltaText)
{
    qDebug() << "[ChatBackend] handleAiStreamDelta received, length:" << deltaText.length();
    if (!m_ttsEnabled || !m_ttsInitialized || !m_tts || deltaText.isEmpty()) {
        qDebug() << "[ChatBackend] handleAiStreamDelta aborted (not ready or disabled)";
        return;
    }

    m_ttsBuffer += deltaText;

    static const QRegularExpression codeBlockRegex(QStringLiteral("```[\\s\\S]*?```"));
    m_ttsBuffer.replace(codeBlockRegex, QStringLiteral(" Code snippet omitted. "));

    splitAndEnqueueSentences();
}

void ChatBackend::handleAiStreamFinished()
{
    qDebug() << "[ChatBackend] handleAiStreamFinished";
    if (!m_ttsEnabled || !m_ttsInitialized || !m_tts) {
        return;
    }

    flushTtsBuffer();
}

void ChatBackend::splitAndEnqueueSentences()
{
    static const QRegularExpression sentenceRegex(QStringLiteral("([.!?])(?:\\s+|$)"));

    QString buffer = m_ttsBuffer;
    QRegularExpressionMatchIterator it = sentenceRegex.globalMatch(buffer);

    int lastIndex = 0;
    QStringList sentences;

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int splitIndex = match.capturedEnd(1);

        sentences.append(buffer.mid(lastIndex, splitIndex - lastIndex));
        lastIndex = splitIndex;
    }

    if (!sentences.isEmpty()) {
        qDebug() << "[ChatBackend] splitAndEnqueue: found" << sentences.size() << "sentences.";
        for (const QString &raw : sentences) {
            const QString cleaned = cleanMarkdown(raw);
            if (!cleaned.isEmpty()) {
                enqueueTtsSentence(cleaned);
            }
        }

        m_ttsBuffer = buffer.mid(lastIndex);
    }
}

void ChatBackend::enqueueTtsSentence(const QString &text)
{
    const QString cleanedText = cleanMarkdown(text);
    if (cleanedText.isEmpty()) {
        return;
    }

    qDebug() << "[ChatBackend] enqueueTtsSentence: text='" << cleanedText.left(50) << "...'";

    // If TTS is not yet initialised, store in pending list
    if (!m_ttsInitialized) {
        qDebug() << "[ChatBackend] TTS not initialized, storing in pending.";
        m_pendingTtsSentences.append(cleanedText);
        return;
    }

    m_ttsQueue.append(cleanedText);
    qDebug() << "[ChatBackend] TTS queue size:" << m_ttsQueue.size();

    if (!m_ttsProcessing) {
        processTtsQueue();
    }
}

void ChatBackend::processTtsQueue()
{
    qDebug() << "[ChatBackend] processTtsQueue called, processing=" << m_ttsProcessing
             << " queueSize=" << m_ttsQueue.size()
             << " enabled=" << m_ttsEnabled;
    if (m_ttsProcessing || m_ttsQueue.isEmpty() || !m_tts || !m_ttsEnabled) {
        return;
    }

    m_ttsProcessing = true;
    const QString sentence = m_ttsQueue.takeFirst();
    qDebug() << "[ChatBackend] Sending sentence to TTS: '" << sentence.left(50) << "...'";
    m_tts->enqueueSentence(sentence, 0);
}

void ChatBackend::handleTtsSentenceFinished()
{
    qDebug() << "[ChatBackend] handleTtsSentenceFinished";
    m_ttsProcessing = false;
    processTtsQueue();
}

void ChatBackend::handleTtsError(const QString &error)
{
    qWarning() << "[ChatBackend] TTS error:" << error;
    emit ttsError(error);

    m_ttsProcessing = false;
    processTtsQueue();
}

void ChatBackend::flushTtsBuffer()
{
    if (!m_ttsEnabled || !m_ttsInitialized || !m_tts) {
        return;
    }

    QString remaining = m_ttsBuffer;
    static const QRegularExpression unclosedCodeBlock(QStringLiteral("```[\\s\\S]*$"));
    remaining.replace(unclosedCodeBlock, QStringLiteral(" Code snippet omitted. "));

    const QString cleaned = cleanMarkdown(remaining);
    if (!cleaned.isEmpty()) {
        qDebug() << "[ChatBackend] flushTtsBuffer: enqueuing remaining text";
        enqueueTtsSentence(cleaned);
    }

    m_ttsBuffer.clear();
}

QString ChatBackend::cleanMarkdown(const QString &text) const
{
    QString result = text;

    static const QRegularExpression codeBlockRegex(QStringLiteral("```[\\s\\S]*?```"));
    result.replace(codeBlockRegex, QStringLiteral(" Code snippet omitted. "));

    static const QRegularExpression inlineCodeRegex(QStringLiteral("`([^`]+)`"));
    QRegularExpressionMatchIterator it = inlineCodeRegex.globalMatch(result);

    QString withoutInlineCode;
    int lastIndex = 0;

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        withoutInlineCode += result.mid(lastIndex, match.capturedStart() - lastIndex);
        withoutInlineCode += match.captured(1);
        lastIndex = match.capturedEnd();
    }

    withoutInlineCode += result.mid(lastIndex);
    result = withoutInlineCode;

    static const QRegularExpression markdownCharsRegex(QStringLiteral("[*_#~\\[\\]]"));
    result.replace(markdownCharsRegex, QString());

    static const QRegularExpression whitespaceRegex(QStringLiteral("\\s+"));
    result.replace(whitespaceRegex, QStringLiteral(" "));

    return result.trimmed();
}

void ChatBackend::requestCapture()
{
    emit captureRequested();
}

void ChatBackend::requestToggleMic()
{
    emit micToggleRequested();
}