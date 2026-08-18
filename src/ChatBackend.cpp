#include "../include/ChatBackend.h"
#include "../include/TtsManager.h"

#include <QDebug>
#include <QRegularExpression>

#include <utility>

ChatBackend::ChatBackend(QObject *parent)
    : QObject(parent)
    , m_tts(new TtsManager(this))
{
    setupTts();
}

ChatBackend::~ChatBackend() = default;

void ChatBackend::setupTts()
{
    if (!m_tts)
        return;

    m_ttsVoice = m_tts->voice();
    m_ttsVoices = m_tts->availableVoices();

    connect(
        m_tts,
        &TtsManager::sentenceFinished,
        this,
        &ChatBackend::handleTtsSentenceFinished
    );

    connect(
        m_tts,
        &TtsManager::errorOccurred,
        this,
        &ChatBackend::handleTtsError
    );

    connect(
        m_tts,
        &TtsManager::serverReady,
        this,
        &ChatBackend::onTtsServerReady
    );

    connect(
        m_tts,
        &TtsManager::voiceChanged,
        this,
        [this](const QString &voice)
        {
            if (voice == m_ttsVoice)
                return;

            m_ttsVoice = voice;
            emit ttsVoiceChanged(m_ttsVoice);
        }
    );

    connect(
        m_tts,
        &TtsManager::voicesChanged,
        this,
        &ChatBackend::handleTtsVoicesChanged
    );

    const bool ok = m_tts->initialize();

    if (!ok) {
        qWarning()
            << "[ChatBackend] TTS initialisation failed."
            << "Docker may be unavailable or permissions may be missing.";

        emit ttsError(
            QStringLiteral(
                "TTS initialisation failed - Docker not available."
            )
        );
    }
}

bool ChatBackend::isTtsReady() const
{
    return m_ttsInitialized && m_tts != nullptr;
}

bool ChatBackend::isTtsEnabled() const
{
    return m_ttsEnabled;
}

QString ChatBackend::ttsVoice() const
{
    return m_ttsVoice;
}

QStringList ChatBackend::ttsVoices() const
{
    return m_ttsVoices;
}

void ChatBackend::setTtsVoice(const QString &voice)
{
    if (!m_tts)
        return;

    const QString normalized = voice.trimmed();

    if (normalized.isEmpty())
        return;

    m_tts->setVoice(normalized);

    const QString actualVoice = m_tts->voice();

    if (actualVoice == m_ttsVoice)
        return;

    m_ttsVoice = actualVoice;

    emit ttsVoiceChanged(m_ttsVoice);
}

void ChatBackend::onTtsServerReady()
{
    m_ttsInitialized = true;

    qDebug()
        << "[ChatBackend] TTS server ready.";

    if (!m_tts)
        return;

    m_tts->setVoice(m_ttsVoice);

    /*
     * Request the current voice catalog.
     */
    m_tts->refreshVoices();

    emit ttsEnabledChanged(m_ttsEnabled);
    emit ttsVoiceChanged(m_ttsVoice);
    emit ttsVoicesChanged(m_ttsVoices);

    /*
     * Do not move pending sentences into the active queue unless
     * TTS is actually enabled.
     */
    if (m_ttsEnabled &&
        !m_pendingTtsSentences.isEmpty()) {

        for (const QString &sentence :
             std::as_const(m_pendingTtsSentences)) {

            m_ttsQueue.append(sentence);
        }

        m_pendingTtsSentences.clear();
    }

    if (m_ttsEnabled)
        processTtsQueue();
}

void ChatBackend::speak(const QString &text)
{
    const QString cleaned = cleanMarkdown(text);

    if (cleaned.isEmpty())
        return;

    if (!m_ttsInitialized) {
        m_pendingTtsSentences.append(cleaned);
        return;
    }

    if (!m_ttsEnabled)
        return;

    enqueueTtsSentence(cleaned);
}

void ChatBackend::stopSpeech()
{
    m_ttsBuffer.clear();
    m_ttsQueue.clear();
    m_pendingTtsSentences.clear();

    m_ttsProcessing = false;

    if (m_tts)
        m_tts->stopAndClear();
}

void ChatBackend::onTtsToggled(bool enabled)
{
    if (m_ttsEnabled == enabled) {
        if (enabled && m_ttsInitialized)
            processTtsQueue();

        return;
    }

    m_ttsEnabled = enabled;

    emit ttsEnabledChanged(m_ttsEnabled);

    if (!m_tts)
        return;

    if (!enabled) {
        m_tts->setEnabled(false);

        m_ttsBuffer.clear();
        m_ttsQueue.clear();
        m_pendingTtsSentences.clear();
        m_ttsProcessing = false;

        return;
    }

    m_tts->setEnabled(true);

    if (!m_pendingTtsSentences.isEmpty()) {
        for (const QString &sentence :
             std::as_const(m_pendingTtsSentences)) {

            m_ttsQueue.append(sentence);
        }

        m_pendingTtsSentences.clear();
    }

    if (m_ttsInitialized)
        processTtsQueue();
}

void ChatBackend::previewTtsVoice()
{
    if (!m_tts ||
        !m_ttsInitialized ||
        !m_ttsEnabled) {
        return;
    }

    enqueueTtsSentence(
        QStringLiteral(
            "This is a preview of the selected voice."
        )
    );
}

void ChatBackend::onUserSendMessage(const QString &message)
{
    stopSpeech();

    const QString cleaned = message.trimmed();

    if (cleaned.isEmpty())
        return;

    emit messageReceived(cleaned);
}

void ChatBackend::handleAiStreamDelta(const QString &deltaText)
{
    if (!m_ttsEnabled ||
        !m_ttsInitialized ||
        !m_tts ||
        deltaText.isEmpty()) {
        return;
    }

    m_ttsBuffer += deltaText;

    static const QRegularExpression codeBlockRegex(
        QStringLiteral("```[\\s\\S]*?```")
    );

    m_ttsBuffer.replace(
        codeBlockRegex,
        QStringLiteral(" Code snippet omitted. ")
    );

    splitAndEnqueueSentences();
}

void ChatBackend::handleAiStreamFinished()
{
    if (!m_ttsEnabled ||
        !m_ttsInitialized ||
        !m_tts) {
        return;
    }

    flushTtsBuffer();
}

void ChatBackend::splitAndEnqueueSentences()
{
    /*
     * Split when sentence-ending punctuation is followed by
     * whitespace.
     *
     * Examples:
     *   "Hello. How are you?"
     *   "He said, \"Hello!\" Then left."
     *
     * The final fragment is intentionally left in m_ttsBuffer
     * until handleAiStreamFinished() calls flushTtsBuffer().
     */
    static const QRegularExpression sentenceRegex(
        QStringLiteral(
            "([.!?…]+[\"'’”»)\\]]*)(?=\\s+)"
        )
    );

    if (!sentenceRegex.isValid()) {
        qWarning()
            << "[ChatBackend] Invalid sentence regex:"
            << sentenceRegex.errorString();

        return;
    }

    const QString buffer = m_ttsBuffer;

    QRegularExpressionMatchIterator iterator =
        sentenceRegex.globalMatch(buffer);

    int lastIndex = 0;

    QStringList sentences;

    while (iterator.hasNext()) {
        const QRegularExpressionMatch match =
            iterator.next();

        if (!match.hasMatch())
            continue;

        const int splitIndex =
            match.capturedEnd(1);

        if (splitIndex <= lastIndex)
            continue;

        sentences.append(
            buffer.mid(
                lastIndex,
                splitIndex - lastIndex
            )
        );

        lastIndex = splitIndex;
    }

    if (sentences.isEmpty())
        return;

    for (const QString &raw : std::as_const(sentences)) {
        const QString cleaned =
            cleanMarkdown(raw);

        if (!cleaned.isEmpty())
            enqueueTtsSentence(cleaned);
    }

    m_ttsBuffer =
        buffer.mid(lastIndex);
}
void ChatBackend::enqueueTtsSentence(const QString &text)
{
    const QString cleanedText =
        cleanMarkdown(text);

    if (cleanedText.isEmpty())
        return;

    if (!m_ttsInitialized) {
        m_pendingTtsSentences.append(cleanedText);
        return;
    }

    if (!m_ttsEnabled)
        return;

    m_ttsQueue.append(cleanedText);

    if (!m_ttsProcessing)
        processTtsQueue();
}

void ChatBackend::processTtsQueue()
{
    if (m_ttsProcessing)
        return;

    if (m_ttsQueue.isEmpty())
        return;

    if (!m_tts)
        return;

    if (!m_ttsInitialized)
        return;

    if (!m_ttsEnabled)
        return;

    const QString sentence =
        m_ttsQueue.takeFirst();

    if (sentence.trimmed().isEmpty()) {
        processTtsQueue();
        return;
    }

    m_ttsProcessing = true;

    m_tts->enqueueSentence(
        sentence,
        0
    );
}

void ChatBackend::handleTtsSentenceFinished()
{
    m_ttsProcessing = false;

    if (!m_ttsEnabled)
        return;

    processTtsQueue();
}

void ChatBackend::handleTtsError(
    const QString &error)
{
    qWarning()
        << "[ChatBackend] TTS error:"
        << error;

    emit ttsError(error);

    m_ttsProcessing = false;

    if (!m_ttsEnabled)
        return;

    processTtsQueue();
}

void ChatBackend::handleTtsVoicesChanged(
    const QStringList &voices)
{
    if (voices.isEmpty())
        return;

    m_ttsVoices = voices;

    emit ttsVoicesChanged(m_ttsVoices);

    /*
     * A blend such as:
     *
     *     af_sky,af_bella
     *
     * is valid but will not appear as an individual catalog entry.
     */
    if (m_ttsVoice.contains(QLatin1Char(',')))
        return;

    if (m_ttsVoices.contains(m_ttsVoice))
        return;

    if (!m_tts)
        return;

    m_tts->setVoice(
        m_ttsVoices.first()
    );

    const QString actualVoice =
        m_tts->voice();

    if (actualVoice == m_ttsVoice)
        return;

    m_ttsVoice = actualVoice;

    emit ttsVoiceChanged(
        m_ttsVoice
    );
}

void ChatBackend::flushTtsBuffer()
{
    if (!m_ttsEnabled ||
        !m_ttsInitialized ||
        !m_tts) {
        return;
    }

    QString remaining = m_ttsBuffer;

    static const QRegularExpression unclosedCodeBlock(
        QStringLiteral(
            "```[\\s\\S]*$"
        )
    );

    remaining.replace(
        unclosedCodeBlock,
        QStringLiteral(
            " Code snippet omitted. "
        )
    );

    const QString cleaned =
        cleanMarkdown(remaining);

    if (!cleaned.isEmpty())
        enqueueTtsSentence(cleaned);

    m_ttsBuffer.clear();
}

QString ChatBackend::cleanMarkdown(
    const QString &text) const
{
    QString result = text;

    static const QRegularExpression codeBlockRegex(
        QStringLiteral(
            "```[\\s\\S]*?```"
        )
    );

    result.replace(
        codeBlockRegex,
        QStringLiteral(
            " Code snippet omitted. "
        )
    );

    static const QRegularExpression inlineCodeRegex(
        QStringLiteral(
            "`([^`]+)`"
        )
    );

    QRegularExpressionMatchIterator iterator =
        inlineCodeRegex.globalMatch(result);

    QString withoutInlineCode;

    int lastIndex = 0;

    while (iterator.hasNext()) {
        const QRegularExpressionMatch match =
            iterator.next();

        withoutInlineCode +=
            result.mid(
                lastIndex,
                match.capturedStart() - lastIndex
            );

        withoutInlineCode +=
            match.captured(1);

        lastIndex =
            match.capturedEnd();
    }

    withoutInlineCode +=
        result.mid(lastIndex);

    result = withoutInlineCode;

    static const QRegularExpression markdownCharsRegex(
        QStringLiteral(
            R"([*_#~\[\]])"
        )
    );

    result.replace(
        markdownCharsRegex,
        QString()
    );

    static const QRegularExpression whitespaceRegex(
        QStringLiteral(
            R"(\s+)"
        )
    );

    result.replace(
        whitespaceRegex,
        QStringLiteral(" ")
    );

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