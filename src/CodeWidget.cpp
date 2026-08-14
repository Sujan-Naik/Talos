#include "CodeWidget.h"
#include "CodeBackend.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QWebEngineView>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QKeyEvent>
#include <QDebug>
#include <QUrl>
#include <QChar>
#include <algorithm>
#include <QVector>

static QString jsonEscape(const QString &str)
{
    QString result;
    result.reserve(str.size() + 2);
    result.append(QLatin1Char('"'));

    for (const QChar &c : str) {
        switch (c.unicode()) {
        case 0x22: result.append(QLatin1String("\\\"")); break;
        case 0x5C: result.append(QLatin1String("\\\\")); break;
        case 0x08: result.append(QLatin1String("\\b")); break;
        case 0x0C: result.append(QLatin1String("\\f")); break;
        case 0x0A: result.append(QLatin1String("\\n")); break;
        case 0x0D: result.append(QLatin1String("\\r")); break;
        case 0x09: result.append(QLatin1String("\\t")); break;
        default:
            if (c.unicode() < 0x20) {
                result.append(QStringLiteral("\\u%1")
                    .arg(c.unicode(), 4, 16, QLatin1Char('0')));
            } else {
                result.append(c);
            }
        }
    }

    result.append(QLatin1Char('"'));
    return result;
}

static QString sanitizeLlmCodeOutput(const QString &rawText)
{
    QString text = rawText.trimmed();

    static const QRegularExpression fenceRegex(
        QStringLiteral("^```[^\\n]*\\n([\\s\\S]*?)\\n?```\\s*$")
    );
    QRegularExpressionMatch match = fenceRegex.match(text);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }

    if (text.startsWith(QStringLiteral("```"))) {
        int firstNewline = text.indexOf(QLatin1Char('\n'));
        if (firstNewline != -1) {
            text = text.mid(firstNewline + 1);
        } else {
            text.clear();
        }
        int lastFence = text.lastIndexOf(QStringLiteral("```"));
        if (lastFence != -1) {
            text = text.left(lastFence).trimmed();
        }
        return text.trimmed();
    }

    return text;
}

// ----------------------------------------------------------------------------
// Extract the first fenced code block from a markdown text.
// Returns the inner code (without language identifier), or empty string.
// ----------------------------------------------------------------------------
static QString extractFirstCodeBlock(const QString &text)
{
    static const QRegularExpression codeBlockRegex(
        QStringLiteral("```[^\\n]*\\n([\\s\\S]*?)```")
    );
    QRegularExpressionMatch match = codeBlockRegex.match(text);
    if (match.hasMatch()) {
        QString code = match.captured(1);
        // Remove trailing newline that may be before closing fence
        while (code.endsWith(QLatin1Char('\n'))) {
            code.chop(1);
        }
        return code;
    }
    return QString();
}

static bool validateEditsArray(const QJsonArray &editsArray, QString *errorString)
{
    for (const QJsonValue &val : editsArray) {
        if (!val.isObject()) {
            if (errorString) *errorString = QStringLiteral("Edit entry is not an object");
            return false;
        }

        QJsonObject obj = val.toObject();

        auto getInt = [&](const QString &key, int defaultValue) -> int {
            if (!obj.contains(key) || !obj.value(key).isDouble())
                return defaultValue;
            return obj.value(key).toInt(defaultValue);
        };

        int startLine = getInt(QStringLiteral("startLine"), 0);
        int startColumn = getInt(QStringLiteral("startColumn"), 0);
        int endLine = getInt(QStringLiteral("endLine"), 0);
        int endColumn = getInt(QStringLiteral("endColumn"), 0);

        if (startLine <= 0 || startColumn <= 0 || endLine <= 0 || endColumn <= 0) {
            if (errorString) *errorString = QStringLiteral("Edit has non-positive line/column numbers");
            return false;
        }
        if (startLine > endLine || (startLine == endLine && startColumn > endColumn)) {
            if (errorString) *errorString = QStringLiteral("Edit range is reversed");
            return false;
        }
    }

    QVector<QPair<int,int>> ranges;
    ranges.reserve(editsArray.size());

    for (const QJsonValue &val : editsArray) {
        QJsonObject obj = val.toObject();
        int startLine = obj.value(QStringLiteral("startLine")).toInt();
        int startColumn = obj.value(QStringLiteral("startColumn")).toInt();
        int endLine = obj.value(QStringLiteral("endLine")).toInt();
        int endColumn = obj.value(QStringLiteral("endColumn")).toInt();
        ranges.append(qMakePair(startLine * 1000000 + startColumn,
                                endLine * 1000000 + endColumn));
    }

    std::sort(ranges.begin(), ranges.end(), [](const QPair<int,int> &a, const QPair<int,int> &b) {
        return a.first < b.first;
    });

    for (int i = 0; i < ranges.size() - 1; ++i) {
        int currEnd = ranges[i].second;
        int nextStart = ranges[i+1].first;
        if (currEnd > nextStart) {
            if (errorString) *errorString = QStringLiteral("Edit ranges overlap");
            return false;
        }
    }

    return true;
}

CodeWidget::CodeWidget(QWidget *parent)
    : QWidget(parent)
    , m_webEngineView(new QWebEngineView(this))
    , m_inputBox(new QTextEdit(this))
    , m_sendButton(new QPushButton(tr("Send"), this))
    , m_clearButton(new QPushButton(tr("Clear"), this))
    , m_statusLabel(new QLabel(tr("Ready"), this))
    , m_networkManager(new QNetworkAccessManager(this))
    , m_backend(new CodeBackend(this))
{
    auto *bottomPanel = new QWidget(this);
    bottomPanel->setObjectName(QStringLiteral("chatPanel"));
    bottomPanel->setStyleSheet(QStringLiteral(R"(
        #chatPanel {
            background: #27272a;
            border-top: 1px solid #3f3f46;
        }
        QLabel#statusLabel {
            color: #a1a1aa;
            font-size: 11px;
            padding: 2px 4px;
        }
        QTextEdit {
            background: #18181b;
            color: #f4f4f5;
            border: 1px solid #3f3f46;
            border-radius: 6px;
            padding: 6px 8px;
            font-size: 13px;
            selection-background-color: #3b82f6;
        }
        QPushButton {
            background: #3f3f46;
            color: #f4f4f5;
            border: none;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: 12px;
        }
        QPushButton:hover {
            background: #52525b;
        }
        QPushButton:disabled {
            background: #3f3f46;
            color: #71717a;
        }
        QPushButton#clearButton {
            background: #52525b;
        }
        QPushButton#clearButton:hover {
            background: #71717a;
        }
        QPushButton#sendButton {
            background: #3b82f6;
            color: white;
            font-weight: 500;
        }
        QPushButton#sendButton:hover {
            background: #2563eb;
        }
        QPushButton#sendButton:disabled {
            background: #1d4ed8;
            color: #bfdbfe;
        }
    )"));

    m_inputBox->setFixedHeight(56);
    m_inputBox->setPlaceholderText(
        tr("Ask AI to edit, explain, or generate code... (Shift+Enter for newline)")
    );
    m_inputBox->installEventFilter(this);

    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_clearButton->setObjectName(QStringLiteral("clearButton"));
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));

    auto *statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(12, 4, 12, 0);
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch();

    auto *inputRow = new QHBoxLayout();
    inputRow->setContentsMargins(12, 0, 12, 12);
    inputRow->setSpacing(8);
    inputRow->addWidget(m_inputBox, 1);
    inputRow->addWidget(m_clearButton);
    inputRow->addWidget(m_sendButton);

    auto *bottomLayout = new QVBoxLayout(bottomPanel);
    bottomLayout->setContentsMargins(0, 0, 0, 0);
    bottomLayout->setSpacing(0);
    bottomLayout->addLayout(statusRow);
    bottomLayout->addLayout(inputRow);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(m_webEngineView, 1);
    mainLayout->addWidget(bottomPanel);
    setLayout(mainLayout);

    auto *channel = new QWebChannel(m_webEngineView->page());
    channel->registerObject(QStringLiteral("backend"), m_backend);
    m_webEngineView->page()->setWebChannel(channel);

    connect(m_backend, &CodeBackend::codeUpdated, this, [this](const QString &code) {
        m_currentCode = code;
    });

    connect(m_backend, &CodeBackend::messageReceived, this, [this](const QString &text) {
        if (!text.isEmpty()) {
            appendMessageAsUser(text);
            sendApiRequest();
        }
    });

    connect(m_sendButton, &QPushButton::clicked, this, [this]() {
        const QString text = m_inputBox->toPlainText().trimmed();
        if (!text.isEmpty()) {
            m_inputBox->clear();
            appendMessageAsUser(text);
            sendApiRequest();
        }
    });

    connect(m_clearButton, &QPushButton::clicked, this, [this]() {
        m_conversationHistory = QJsonArray();
        m_inputBox->clear();
        m_webEngineView->page()->runJavaScript(
            QStringLiteral("if (window.clearAnnotations) window.clearAnnotations();")
        );
        m_webEngineView->page()->runJavaScript(
            QStringLiteral("document.getElementById('side-panel').classList.remove('open');")
        );
        m_statusLabel->setText(tr("Ready"));
    });

    connect(m_webEngineView, &QWebEngineView::loadFinished, this, [this](bool ok) {
        m_isPageLoaded = ok;
    });

    m_webEngineView->load(QUrl(QStringLiteral("qrc:///widgets/code/code.html")));
}

void CodeWidget::setEditorCode(const QString &code, const QString &language)
{
    if (!m_isPageLoaded) return;

    const QString jsCode = jsonEscape(code);
    const QString jsLang = jsonEscape(language);

    const QString js = QStringLiteral(
        "if (typeof window.setEditorCode === 'function') window.setEditorCode(%1, %2);"
    ).arg(jsCode, jsLang);

    qDebug() << "[CodeWidget] setEditorCode JS:" << js;
    m_webEngineView->page()->runJavaScript(js);
}

void CodeWidget::appendCodeToEditor(const QString &code)
{
    if (!m_isPageLoaded) return;

    const QString jsCode = jsonEscape(code);

    const QString js = QStringLiteral(
        "if (typeof window.appendCodeToEditor === 'function') window.appendCodeToEditor(%1);"
    ).arg(jsCode);

    qDebug() << "[CodeWidget] appendCodeToEditor JS:" << js;
    m_webEngineView->page()->runJavaScript(js);
}

QString CodeWidget::editorCode() const
{
    return m_currentCode;
}

void CodeWidget::appendMessageAsUser(const QString &text)
{
    QJsonObject msg;
    msg.insert(QStringLiteral("role"), QStringLiteral("user"));
    msg.insert(QStringLiteral("content"), text);
    m_conversationHistory.append(msg);
    emit messageSent(text);
}

void CodeWidget::appendMessageAsAi(const QString &text)
{
    QJsonObject msg;
    msg.insert(QStringLiteral("role"), QStringLiteral("assistant"));
    msg.insert(QStringLiteral("content"), text);
    m_conversationHistory.append(msg);
}

void CodeWidget::showWarningBubble(const QString &message)
{
    if (!m_isPageLoaded) return;

    QJsonObject ann;
    ann.insert(QStringLiteral("startLine"), 1);
    ann.insert(QStringLiteral("endLine"), 1);
    ann.insert(QStringLiteral("message"), message);
    ann.insert(QStringLiteral("severity"), QStringLiteral("warning"));

    QJsonArray arr;
    arr.append(ann);

    const QString annArray = QString::fromUtf8(
        QJsonDocument(arr).toJson(QJsonDocument::Compact)
    );

    const QString js = QStringLiteral(
        "if (window.clearAnnotations) window.clearAnnotations();"
        "if (window.setAnnotations) window.setAnnotations(%1);"
    ).arg(annArray);

    qDebug() << "[CodeWidget] showWarningBubble JS:" << js;
    m_webEngineView->page()->runJavaScript(js);
}

void CodeWidget::sendApiRequest()
{
    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    m_sendButton->setEnabled(false);
    m_clearButton->setEnabled(false);
    m_sendButton->setText(tr("Thinking..."));
    m_statusLabel->setText(tr("Sending request..."));

    QUrl url = QUrl::fromUserInput(QStringLiteral("127.0.0.1:8080/v1/chat/completions"));
    if (url.scheme().isEmpty()) url.setScheme(QStringLiteral("http"));

    if (!url.isValid()) {
        qWarning() << "Failed to construct valid QUrl:" << url.errorString();
        m_statusLabel->setText(tr("Invalid URL error"));
        m_sendButton->setEnabled(true);
        m_clearButton->setEnabled(true);
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(30000);

    QJsonArray messages;

    QJsonObject systemMsg;
    systemMsg.insert(QStringLiteral("role"), QStringLiteral("system"));
    systemMsg.insert(QStringLiteral("content"), QStringLiteral(
        "You are a code editor assistant embedded in Monaco Editor. "
        "You can answer questions about the code, provide explanations, and make precise edits.\n\n"

        "STRICT OUTPUT FORMAT (you MUST output exactly three sections, in this order):\n"
        "<<<EDITS>>>\n"
        "JSON array of edit operations. Each object: {\"startLine\":1,\"startColumn\":1,\"endLine\":1,\"endColumn\":1,\"text\":\"replacement text\"}\n"
        "If no code changes are needed, output empty array: []\n\n"
        "<<<ANSWER>>>\n"
        "General textual answer to the user's question. Can be empty if not applicable.\n\n"
        "<<<ANNOTATIONS>>>\n"
        "JSON array of annotation objects. Each: {\"startLine\":1,\"startColumn\":1,\"endLine\":1,\"endColumn\":1,\"message\":\"...\",\"severity\":\"info|warning|error\"}\n"
        "If no line-specific comments, output empty array: []\n\n"

        "RULES (must be followed exactly):\n"
        "- Begin your response with <<<EDITS>>> immediately.\n"
        "- Do NOT add any text before the first tag.\n"
        "- NEVER output the whole file in EDITS. Only give the exact changed ranges.\n"
        "- Use 1-based line and column numbers based on the CURRENT EDITOR CODE below.\n"
        "- For deletion, set \"text\" to \"\".\n"
        "- Never output text outside these sections.\n"
        "- EDITS and ANNOTATIONS must be valid JSON arrays.\n"
        "- If user asks a general question, answer in ANSWER section, and use ANNOTATIONS for line-specific details if needed.\n"
        "- If user asks for code changes, use EDITS section.\n"
        "- Do NOT wrap the sections in markdown code fences. Output only the tags and their content.\n"
        "- The code below is shown WITHOUT line numbers. Count lines carefully; line 1 is the first line.\n\n"

        "=== CURRENT EDITOR CODE ===\n%1"
    ).arg(m_currentCode));
    messages.append(systemMsg);

    for (const QJsonValue &m : m_conversationHistory) {
        messages.append(m);
    }

    QJsonObject body;
    body.insert(QStringLiteral("model"), QStringLiteral("local-model"));
    body.insert(QStringLiteral("messages"), messages);
    body.insert(QStringLiteral("stream"), true);

    m_streamBuffer.clear();
    m_aiStreamAccumulator.clear();
    m_isStreamingAi = true;

    m_currentReply = m_networkManager->post(request, QJsonDocument(body).toJson());

    connect(m_currentReply, &QNetworkReply::readyRead, this, &CodeWidget::handleReadyRead);
    connect(m_currentReply, &QNetworkReply::finished, this, &CodeWidget::handleReplyFinished);
    connect(m_currentReply, &QNetworkReply::errorOccurred, this, [this](QNetworkReply::NetworkError error) {
        qWarning() << "LLM request error:" << error;
        m_sendButton->setEnabled(true);
        m_clearButton->setEnabled(true);
        m_sendButton->setText(tr("Send"));
        m_statusLabel->setText(tr("Request error"));
        m_isStreamingAi = false;
    });
}

void CodeWidget::handleReadyRead()
{
    if (!m_currentReply) return;

    m_streamBuffer.append(m_currentReply->readAll());

    while (m_streamBuffer.contains('\n')) {
        const int idx = m_streamBuffer.indexOf('\n');
        QByteArray line = m_streamBuffer.left(idx).trimmed();
        m_streamBuffer.remove(0, idx + 1);

        if (line.isEmpty()) continue;
        if (line.startsWith("data: ")) line = line.mid(6).trimmed();
        if (line == "[DONE]") break;

        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error != QJsonParseError::NoError || !doc.isObject()) continue;

        const QJsonObject root = doc.object();
        QString delta;

        if (root.contains(QStringLiteral("choices"))) {
            const QJsonArray choices = root.value(QStringLiteral("choices")).toArray();
            if (!choices.isEmpty()) {
                const QJsonObject choice = choices.at(0).toObject();
                if (choice.contains(QStringLiteral("delta"))) {
                    const QJsonObject d = choice.value(QStringLiteral("delta")).toObject();
                    if (d.contains(QStringLiteral("content"))) {
                        delta = d.value(QStringLiteral("content")).toString();
                    }
                } else if (choice.contains(QStringLiteral("text"))) {
                    delta = choice.value(QStringLiteral("text")).toString();
                }
            }
        } else if (root.contains(QStringLiteral("response"))) {
            delta = root.value(QStringLiteral("response")).toString();
        } else if (root.contains(QStringLiteral("message"))) {
            const QJsonObject msg = root.value(QStringLiteral("message")).toObject();
            if (msg.contains(QStringLiteral("content"))) {
                delta = msg.value(QStringLiteral("content")).toString();
            }
        }

        if (!delta.isEmpty()) {
            m_aiStreamAccumulator.append(delta);
            m_statusLabel->setText(tr("Streaming..."));

            // ---- LIVE STREAMING (fallback if no tags yet) ----
            const QString answerTag = QStringLiteral("<<<ANSWER>>>");
            const QString editsTag = QStringLiteral("<<<EDITS>>>");
            const QString annTag = QStringLiteral("<<<ANNOTATIONS>>>");

            int ansIdx = m_aiStreamAccumulator.indexOf(answerTag);
            int editsIdx = m_aiStreamAccumulator.indexOf(editsTag);
            int annIdx = m_aiStreamAccumulator.indexOf(annTag);

            // If no tags have appeared yet, treat the whole accumulated text as answer
            if (ansIdx == -1 && editsIdx == -1 && annIdx == -1) {
                QString partialAnswer = m_aiStreamAccumulator.trimmed();
                if (!partialAnswer.isEmpty()) {
                    const QString js = QStringLiteral(
                        "if (window.showAnswer) window.showAnswer(%1);"
                    ).arg(jsonEscape(partialAnswer));
                    m_webEngineView->page()->runJavaScript(js);
                }
            } else if (ansIdx != -1) {
                // We have an ANSWER tag; stream its content
                QString partialAnswer;
                int start = ansIdx + answerTag.length();

                if (annIdx != -1 && annIdx > start) {
                    partialAnswer = m_aiStreamAccumulator.mid(start, annIdx - start);
                } else if (annIdx == -1) {
                    partialAnswer = m_aiStreamAccumulator.mid(start);
                }

                if (!partialAnswer.isEmpty()) {
                    const QString js = QStringLiteral(
                        "if (window.showAnswer) window.showAnswer(%1);"
                    ).arg(jsonEscape(partialAnswer));
                    m_webEngineView->page()->runJavaScript(js);
                }
            }
        }
    }
}

void CodeWidget::handleReplyFinished()
{
    if (m_currentReply) {
        if (m_currentReply->error() == QNetworkReply::NoError) {
            const QString full = m_aiStreamAccumulator.trimmed();
            qDebug() << "[CodeWidget] Full LLM response:" << full;

            const QString fullSanitized = sanitizeLlmCodeOutput(full);

            const QString editsTag = QStringLiteral("<<<EDITS>>>");
            const QString answerTag = QStringLiteral("<<<ANSWER>>>");
            const QString annTag = QStringLiteral("<<<ANNOTATIONS>>>");

            int editsIdx = fullSanitized.indexOf(editsTag);
            int answerIdx = fullSanitized.indexOf(answerTag);
            int annIdx = fullSanitized.indexOf(annTag);

            QString editsPart;
            QString answerPart;
            QString annPart;

            // If no tags found at all, treat entire response as answer
            if (editsIdx == -1 && answerIdx == -1 && annIdx == -1) {
                answerPart = fullSanitized;
            } else {
                // Parse sections as before
                if (editsIdx != -1) {
                    int editStart = editsIdx + editsTag.length();
                    int editEnd = fullSanitized.length();
                    if (answerIdx != -1 && answerIdx > editStart) editEnd = answerIdx;
                    else if (annIdx != -1 && annIdx > editStart) editEnd = annIdx;
                    editsPart = fullSanitized.mid(editStart, editEnd - editStart).trimmed();
                }

                if (answerIdx != -1) {
                    int answerStart = answerIdx + answerTag.length();
                    int answerEnd = fullSanitized.length();
                    if (annIdx != -1 && annIdx > answerStart) answerEnd = annIdx;
                    answerPart = fullSanitized.mid(answerStart, answerEnd - answerStart).trimmed();
                }

                if (annIdx != -1) {
                    int annStart = annIdx + annTag.length();
                    annPart = fullSanitized.mid(annStart).trimmed();
                }
            }

            QString editsJson = sanitizeLlmCodeOutput(editsPart);
            QString annotationsJson = sanitizeLlmCodeOutput(annPart);

            qDebug() << "[CodeWidget] Edits JSON:" << editsJson;
            qDebug() << "[CodeWidget] Answer:" << answerPart;
            qDebug() << "[CodeWidget] Annotations JSON:" << annotationsJson;

            // Apply edits if any and valid
            if (!editsJson.isEmpty()) {
                QJsonParseError editsParseError;
                const QJsonDocument editsDoc = QJsonDocument::fromJson(editsJson.toUtf8(), &editsParseError);
                if (editsParseError.error == QJsonParseError::NoError && editsDoc.isArray()) {
                    QJsonArray editsArray = editsDoc.array();
                    QString validationError;
                    if (validateEditsArray(editsArray, &validationError)) {
                        if (!editsArray.isEmpty()) {
                            const QString editsJsonCompact = QString::fromUtf8(editsDoc.toJson(QJsonDocument::Compact));
                            const QString js = QStringLiteral(
                                "if (window.applyEdits) window.applyEdits(%1);"
                            ).arg(editsJsonCompact);
                            qDebug() << "[CodeWidget] applyEdits JS:" << js;
                            m_webEngineView->page()->runJavaScript(js);
                        }
                    } else {
                        qWarning() << "Edits validation failed:" << validationError;
                        showWarningBubble(tr("AI returned invalid edit data: %1").arg(validationError));
                    }
                } else {
                    qWarning() << "Invalid edits JSON:" << editsJson;
                    showWarningBubble(tr("AI returned invalid edit data."));
                }
            } else {
                // No structured edits. If answer contains code fences, apply first code block
                QString codeBlock = extractFirstCodeBlock(answerPart);
                if (!codeBlock.isEmpty()) {
                    qDebug() << "[CodeWidget] No edits found, but answer has code block. Applying as whole file.";
                    setEditorCode(codeBlock);
                }
            }

            // Show final answer (markdown formatted in JS)
            if (!answerPart.isEmpty()) {
                const QString jsAnswer = jsonEscape(answerPart);
                const QString js = QStringLiteral(
                    "if (window.showAnswer) window.showAnswer(%1);"
                ).arg(jsAnswer);
                qDebug() << "[CodeWidget] showAnswer JS:" << js;
                m_webEngineView->page()->runJavaScript(js);
            }

            // Render annotations if valid
            QJsonParseError annParseError;
            const QJsonDocument annDoc = QJsonDocument::fromJson(annotationsJson.toUtf8(), &annParseError);
            if (annParseError.error == QJsonParseError::NoError && annDoc.isArray()) {
                const QString annArrayCompact = QString::fromUtf8(annDoc.toJson(QJsonDocument::Compact));
                const QString js = QStringLiteral(
                    "if (window.clearAnnotations) window.clearAnnotations();"
                    "if (window.setAnnotations) window.setAnnotations(%1);"
                ).arg(annArrayCompact);
                qDebug() << "[CodeWidget] annotations JS:" << js;
                m_webEngineView->page()->runJavaScript(js);
            } else {
                if (!annotationsJson.isEmpty()) {
                    qWarning() << "Invalid annotations JSON:" << annotationsJson;
                    showWarningBubble(tr("AI returned invalid annotation data."));
                }
            }

            appendMessageAsAi(full);
        } else {
            qWarning() << "LLM request failed:" << m_currentReply->errorString();
            m_statusLabel->setText(tr("Request failed"));
        }

        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    m_sendButton->setEnabled(true);
    m_clearButton->setEnabled(true);
    m_sendButton->setText(tr("Send"));
    m_statusLabel->setText(tr("Ready"));
    m_isStreamingAi = false;
}

bool CodeWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_inputBox && event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(event);
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && !(ke->modifiers() & Qt::ShiftModifier)) {
            m_sendButton->click();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}