#include "CodeWidget.h"
#include "CodeBackend.h"
#include "ResponseProcessor.h"
#include "ApiStreamClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTextEdit>
#include <QWebChannel>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QNetworkAccessManager>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QEvent>
#include <QKeyEvent>

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

CodeWidget::CodeWidget(QWidget *parent)
    : QWidget(parent)
    , m_webEngineView(new QWebEngineView(this))
    , m_inputBox(new QTextEdit(this))
    , m_sendButton(new QPushButton(tr("Send"), this))
    , m_clearButton(new QPushButton(tr("Clear"), this))
    , m_statusLabel(new QLabel(tr("Ready"), this))
    , m_backend(new CodeBackend(this))
    , m_apiClient(std::make_unique<ApiStreamClient>(new QNetworkAccessManager(this), this))
    , m_executor(std::make_unique<EditorCommandExecutor>())
{
    setupUi();
    setupConnections();
}

void CodeWidget::setupUi()
{
    auto *bottomPanel = new QWidget(this);
    bottomPanel->setObjectName(QStringLiteral("chatPanel"));
    bottomPanel->setStyleSheet(QStringLiteral(R"(
        #chatPanel { background: #27272a; border-top: 1px solid #3f3f46; }
        QLabel#statusLabel { color: #a1a1aa; font-size: 11px; padding: 2px 4px; }
        QTextEdit { background: #18181b; color: #f4f4f5; border: 1px solid #3f3f46;
                    border-radius: 6px; padding: 6px 8px; font-size: 13px; }
        QPushButton { background: #3f3f46; color: #f4f4f5; border: none; border-radius: 6px;
                      padding: 6px 12px; font-size: 12px; }
        QPushButton:hover { background: #52525b; }
        QPushButton:disabled { background: #27272a; color: #52525b; border: 1px solid #3f3f46; }
        QPushButton#sendButton { background: #3b82f6; color: white; font-weight: 500; }
        QPushButton#sendButton:hover { background: #2563eb; }
        QPushButton#contextButton { background: transparent; border: 1px solid #3f3f46; }
        QPushButton#contextButton:hover { background: #3f3f46; }
    )"));

    m_inputBox->setFixedHeight(56);
    m_inputBox->setPlaceholderText(tr("Ask AI to edit, explain, or generate code..."));
    m_inputBox->installEventFilter(this);

    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_clearButton->setObjectName(QStringLiteral("clearButton"));
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));

    // Context button - greyed out for now
    QPushButton *contextButton = new QPushButton(tr("Context"), this);
    contextButton->setObjectName(QStringLiteral("contextButton"));
    contextButton->setEnabled(false);  // Greyed out
    contextButton->setToolTip(tr("Context settings (coming soon)"));

    auto *statusRow = new QHBoxLayout();
    statusRow->setContentsMargins(12, 4, 12, 0);
    statusRow->addWidget(m_statusLabel);
    statusRow->addStretch();
    statusRow->addWidget(contextButton);

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
}

void CodeWidget::setupConnections()
{
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

    connect(m_apiClient.get(), &ApiStreamClient::eventReceived, this, &CodeWidget::onStreamEvent);
    connect(m_apiClient.get(), &ApiStreamClient::requestFinished, this, &CodeWidget::onRequestFinished);
    connect(m_apiClient.get(), &ApiStreamClient::requestError, this, &CodeWidget::onRequestError);

    m_webEngineView->load(QUrl(QStringLiteral("qrc:///widgets/code/code.html")));
}

void CodeWidget::setEditorCode(const QString &code, const QString &language)
{
    if (!m_isPageLoaded) return;
    const QString jsCode = jsonEscape(code);
    const QString jsLang = jsonEscape(language);
    m_webEngineView->page()->runJavaScript(
        QStringLiteral("if (typeof window.setEditorCode === 'function') window.setEditorCode(%1, %2);")
        .arg(jsCode, jsLang)
    );
}

QString CodeWidget::editorCode() const
{
    return m_currentCode;
}

void CodeWidget::setContextLevel(LlmPromptBuilder::CodeContextLevel level)
{
    m_contextLevel = level;
}

void CodeWidget::setSelectionMode(LlmPromptBuilder::ContextSelectionMode mode)
{
    m_selectionMode = mode;
}

void CodeWidget::setRepoStructure(const QString &structure)
{
    m_repoStructure = structure;
}

void CodeWidget::setCurrentFile(const QString &filePath)
{
    m_currentFile = filePath;
}

void CodeWidget::setCurrentDirectory(const QString &dirPath)
{
    m_currentDirectory = dirPath;
}

void CodeWidget::setMaxHistoryMessages(int maxMessages)
{
    m_maxRecentMessages = maxMessages;
}

int CodeWidget::maxHistoryMessages() const
{
    return m_maxRecentMessages;
}

void CodeWidget::onContextLevelChanged(int index)
{
    Q_UNUSED(index);
    // Future: Handle context level changes
    qDebug() << "[CodeWidget] Context level changed to:" << index;
}

void CodeWidget::onSelectionModeChanged(int index)
{
    Q_UNUSED(index);
    // Future: Handle selection mode changes
    qDebug() << "[CodeWidget] Selection mode changed to:" << index;
}

void CodeWidget::onHistoryLengthChanged(int value)
{
    m_maxRecentMessages = value;
    qDebug() << "[CodeWidget] Max history messages changed to:" << value;
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

void CodeWidget::sendApiRequest()
{
    if (m_apiClient->isActive()) {
        m_apiClient->abortRequest();
    }

    m_sendButton->setEnabled(false);
    m_clearButton->setEnabled(false);
    m_sendButton->setText(tr("Thinking..."));
    m_statusLabel->setText(tr("Sending request..."));

    m_aiStreamAccumulator.clear();

    LlmPromptBuilder::PromptContext ctx;

    // History
    ctx.fullHistory = m_conversationHistory;
    ctx.maxRecentMessages = m_maxRecentMessages;

    // Code context - always send the current editor code
    ctx.currentCode = m_currentCode;
    ctx.currentFile = m_currentFile;
    ctx.currentDirectory = m_currentDirectory;
    ctx.repoStructure = m_repoStructure;
    ctx.contextLevel = LlmPromptBuilder::CodeContextLevel::File;  // Always File for now

    // Model settings
    ctx.model = QStringLiteral("local-model");
    ctx.temperature = 0.15;

    ApiStreamClient::RequestParams params;
    params.url = QStringLiteral("http://127.0.0.1:8080/v1/chat/completions");
    params.messages = m_promptBuilder.buildMessages(ctx);
    params.model = ctx.model;
    params.temperature = ctx.temperature;
    params.timeoutMs = 120000;

    m_apiClient->sendRequest(params);
}

void CodeWidget::onStreamEvent(const StreamEvent &event)
{
    if (event.type != StreamEventType::TextDelta || event.data.isEmpty()) return;

    m_aiStreamAccumulator.append(event.data);
    m_statusLabel->setText(tr("Streaming..."));

    // Live display of answer section
    const QString answerTag = QStringLiteral("<<<ANSWER>>>");
    int ansIdx = m_aiStreamAccumulator.indexOf(answerTag);
    if (ansIdx != -1) {
        int start = ansIdx + answerTag.length();
        const QString annTag = QStringLiteral("<<<ANNOTATIONS>>>");
        int annIdx = m_aiStreamAccumulator.indexOf(annTag);
        int end = (annIdx != -1) ? annIdx : m_aiStreamAccumulator.length();

        QString partialAnswer = m_aiStreamAccumulator.mid(start, end - start).trimmed();
        if (!partialAnswer.isEmpty()) {
            showAnswer(partialAnswer);
        }
    }
}

void CodeWidget::onRequestFinished()
{
    const QString full = m_aiStreamAccumulator.trimmed();
    qDebug() << "[CodeWidget] Full LLM response received";

    ParsedResponse parsed = m_responseProcessor.parseStructuredResponse(full);

    auto applySearchReplace = [this](const QString &json) {
        const QString js = QStringLiteral("if (window.applySearchReplace) window.applySearchReplace(%1);")
            .arg(json);
        m_webEngineView->page()->runJavaScript(js);
    };

    auto applyRangeEdits = [this](const QString &json) {
        const QString js = QStringLiteral("if (window.applyEdits) window.applyEdits(%1);")
            .arg(json);
        m_webEngineView->page()->runJavaScript(js);
    };

    auto setFullCode = [this](const QString &code) {
        const QString js = QStringLiteral("if (typeof window.setEditorCode === 'function') window.setEditorCode(%1, '');")
            .arg(jsonEscape(code));
        m_webEngineView->page()->runJavaScript(js);
    };

    EditorCommandExecutor::ExecutionResult execResult = m_executor->execute(
        parsed, m_currentCode, applySearchReplace, applyRangeEdits, setFullCode
    );

    if (!execResult.success && !execResult.message.isEmpty()) {
        showWarningBubble(execResult.message);
    }

    // Display answer
    if (!parsed.answer.isEmpty()) {
        showAnswer(parsed.answer);
    }

    // Display annotations
    QJsonArray annotations = m_responseProcessor.parseAnnotations(parsed.annotations);
    if (!annotations.isEmpty()) {
        setAnnotations(QString::fromUtf8(QJsonDocument(annotations).toJson(QJsonDocument::Compact)));
    }

    // Store only the answer, not the raw structured response
    appendMessageAsAi(parsed.answer);

    m_sendButton->setEnabled(true);
    m_clearButton->setEnabled(true);
    m_sendButton->setText(tr("Send"));
    m_statusLabel->setText(tr("Ready"));
}

void CodeWidget::onRequestError(const QString &error)
{
    qWarning() << "API request error:" << error;
    showWarningBubble(error);
    m_sendButton->setEnabled(true);
    m_clearButton->setEnabled(true);
    m_sendButton->setText(tr("Send"));
    m_statusLabel->setText(tr("Request error"));
}

void CodeWidget::showAnswer(const QString &text)
{
    if (!m_isPageLoaded) return;
    const QString js = QStringLiteral("if (window.showAnswer) window.showAnswer(%1);")
        .arg(jsonEscape(text));
    m_webEngineView->page()->runJavaScript(js);
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
    setAnnotations(QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

void CodeWidget::setAnnotations(const QString &json)
{
    if (!m_isPageLoaded) return;
    const QString js = QStringLiteral(
        "if (window.clearAnnotations) window.clearAnnotations();"
        "if (window.setAnnotations) window.setAnnotations(%1);"
    ).arg(json);
    m_webEngineView->page()->runJavaScript(js);
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