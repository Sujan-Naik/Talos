#include "CodeWidget.h"

#include "CodeBackend.h"
#include "CodingAgent.h"
#include "ProjectModel.h"
#include "ResponseProcessor.h"

#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QSizePolicy>
#include <QTextEdit>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineView>

namespace
{
const QString kLastProjectSetting =
    QStringLiteral(
        "lastProjectDirectory"
    );

const QString kAiEndpointSetting =
    QStringLiteral(
        "ai/endpoint"
    );

const QString kAiModelSetting =
    QStringLiteral(
        "ai/model"
    );

const QString kDefaultAiEndpoint =
    QStringLiteral(
        "http://127.0.0.1:8080/v1"
    );

QSettings talosSettings()
{
    return QSettings(
        QStringLiteral("Talos"),
        QStringLiteral("Talos")
    );
}

QString jsonEscape(
    const QString &str
)
{
    QString result;

    result.reserve(
        str.size() + 2
    );

    result.append(
        QLatin1Char('"')
    );

    for (
        const QChar &c :
        str
    ) {
        switch (c.unicode()) {
        case 0x22:
            result.append(
                QLatin1String("\\\"")
            );
            break;

        case 0x5C:
            result.append(
                QLatin1String("\\\\")
            );
            break;

        case 0x08:
            result.append(
                QLatin1String("\\b")
            );
            break;

        case 0x0C:
            result.append(
                QLatin1String("\\f")
            );
            break;

        case 0x0A:
            result.append(
                QLatin1String("\\n")
            );
            break;

        case 0x0D:
            result.append(
                QLatin1String("\\r")
            );
            break;

        case 0x09:
            result.append(
                QLatin1String("\\t")
            );
            break;

        default:
            if (c.unicode() < 0x20) {
                result.append(
                    QStringLiteral("\\u%1")
                        .arg(
                            c.unicode(),
                            4,
                            16,
                            QLatin1Char('0')
                        )
                );
            } else {
                result.append(c);
            }
        }
    }

    result.append(
        QLatin1Char('"')
    );

    return result;
}
}

CodeWidget::CodeWidget(
    QWidget *parent
)
    : QWidget(parent)
    , m_webEngineView(
        new QWebEngineView(this)
    )
    , m_inputBox(
        new QTextEdit(this)
    )
    , m_sendButton(
        new QPushButton(
            tr("Send"),
            this
        )
    )
    , m_clearButton(
        new QPushButton(
            tr("Clear"),
            this
        )
    )
    , m_openProjectButton(
        new QPushButton(
            tr("Open Project"),
            this
        )
    )
    , m_refreshModelsButton(
        new QPushButton(
            tr("↻"),
            this
        )
    )
    , m_endpointEdit(
        new QLineEdit(this)
    )
    , m_modelCombo(
        new QComboBox(this)
    )
    , m_statusLabel(
        new QLabel(
            tr("Ready"),
            this
        )
    )
    , m_projectLabel(
        new QLabel(
            tr("No project"),
            this
        )
    )
    , m_modelStatusLabel(
        new QLabel(
            this
        )
    )
    , m_backend(
        new CodeBackend(this)
    )
    , m_projectModel(
        new ProjectModel(this)
    )
    , m_agent(
        new CodingAgent(
            m_projectModel,
            this
        )
    )
    , m_modelNetworkManager(
        new QNetworkAccessManager(this)
    )
    , m_executor(
        std::make_unique<
            EditorCommandExecutor
        >()
    )
{
    setupUi();
    setupConnections();

    restoreAiSettings();
    restoreLastProject();

    refreshModelList();
}

void CodeWidget::setupUi()
{
    auto *bottomPanel =
        new QWidget(this);

    bottomPanel->setObjectName(
        QStringLiteral("chatPanel")
    );

    bottomPanel->setStyleSheet(
        QStringLiteral(R"(
            #chatPanel {
                background: #27272a;
                border-top: 1px solid #3f3f46;
            }

            QLabel#statusLabel {
                color: #a1a1aa;
                font-size: 11px;
                padding: 2px 4px;
            }

            QLabel#projectLabel {
                color: #a1a1aa;
                font-size: 11px;
                padding: 2px 6px;
            }

            QLabel#modelStatusLabel {
                color: #71717a;
                font-size: 10px;
                padding: 2px 4px;
            }

            QLineEdit#endpointEdit {
                background: #18181b;
                color: #e4e4e7;
                border: 1px solid #3f3f46;
                border-radius: 6px;
                padding: 5px 8px;
                font-size: 11px;
            }

            QComboBox#modelCombo {
                background: #18181b;
                color: #e4e4e7;
                border: 1px solid #3f3f46;
                border-radius: 6px;
                padding: 5px 8px;
                font-size: 11px;
            }

            QComboBox#modelCombo QAbstractItemView {
                background: #18181b;
                color: #e4e4e7;
                selection-background-color: #3f3f46;
                selection-color: #ffffff;
            }

            QPushButton#openProjectButton,
            QPushButton#refreshModelsButton {
                background: #3f3f46;
                color: #f4f4f5;
                border: none;
                border-radius: 6px;
                padding: 6px 10px;
                font-size: 12px;
            }

            QPushButton#openProjectButton:hover,
            QPushButton#refreshModelsButton:hover {
                background: #52525b;
            }

            QTextEdit {
                background: #18181b;
                color: #f4f4f5;
                border: 1px solid #3f3f46;
                border-radius: 6px;
                padding: 6px 8px;
                font-size: 13px;
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
                background: #27272a;
                color: #52525b;
                border: 1px solid #3f3f46;
            }

            QPushButton#sendButton {
                background: #3b82f6;
                color: white;
                font-weight: 500;
            }

            QPushButton#sendButton:hover {
                background: #2563eb;
            }

            QPushButton#contextButton {
                background: transparent;
                border: 1px solid #3f3f46;
            }

            QPushButton#contextButton:hover {
                background: #3f3f46;
            }
        )")
    );

    m_inputBox->setFixedHeight(56);

    m_inputBox->setPlaceholderText(
        tr(
            "Ask AI to explain, review, or improve code..."
        )
    );

    m_inputBox->installEventFilter(
        this
    );

    m_sendButton->setObjectName(
        QStringLiteral("sendButton")
    );

    m_clearButton->setObjectName(
        QStringLiteral("clearButton")
    );

    m_openProjectButton->setObjectName(
        QStringLiteral(
            "openProjectButton"
        )
    );

    m_refreshModelsButton->setObjectName(
        QStringLiteral(
            "refreshModelsButton"
        )
    );

    m_endpointEdit->setObjectName(
        QStringLiteral(
            "endpointEdit"
        )
    );

    m_modelCombo->setObjectName(
        QStringLiteral(
            "modelCombo"
        )
    );

    m_statusLabel->setObjectName(
        QStringLiteral(
            "statusLabel"
        )
    );

    m_projectLabel->setObjectName(
        QStringLiteral(
            "projectLabel"
        )
    );

    m_modelStatusLabel->setObjectName(
        QStringLiteral(
            "modelStatusLabel"
        )
    );

    m_endpointEdit->setPlaceholderText(
        tr(
            "OpenAI-compatible API base URL"
        )
    );

    m_endpointEdit->setMinimumWidth(
        260
    );

    m_endpointEdit->setMaximumWidth(
        420
    );

    m_modelCombo->setEditable(
        true
    );

    m_modelCombo->setInsertPolicy(
        QComboBox::NoInsert
    );

    m_modelCombo->setMinimumWidth(
        230
    );

    m_modelCombo->setMaximumWidth(
        420
    );

    m_projectLabel->setSizePolicy(
        QSizePolicy::Ignored,
        QSizePolicy::Preferred
    );

    m_projectLabel->setMinimumWidth(
        100
    );

    m_projectLabel->setMaximumWidth(
        400
    );

    auto *contextButton =
        new QPushButton(
            tr("Context"),
            this
        );

    contextButton->setObjectName(
        QStringLiteral(
            "contextButton"
        )
    );

    contextButton->setEnabled(
        false
    );

    auto *aiRow =
        new QHBoxLayout();

    aiRow->setContentsMargins(
        12,
        4,
        12,
        0
    );

    aiRow->setSpacing(
        6
    );

    aiRow->addWidget(
        new QLabel(
            tr("API"),
            this
        )
    );

    aiRow->addWidget(
        m_endpointEdit,
        1
    );

    aiRow->addWidget(
        new QLabel(
            tr("Model"),
            this
        )
    );

    aiRow->addWidget(
        m_modelCombo,
        1
    );

    aiRow->addWidget(
        m_refreshModelsButton
    );

    aiRow->addWidget(
        m_modelStatusLabel
    );

    auto *statusRow =
        new QHBoxLayout();

    statusRow->setContentsMargins(
        12,
        4,
        12,
        0
    );

    statusRow->setSpacing(
        8
    );

    statusRow->addWidget(
        m_openProjectButton
    );

    statusRow->addWidget(
        m_projectLabel,
        1
    );

    statusRow->addWidget(
        m_statusLabel
    );

    statusRow->addStretch();

    statusRow->addWidget(
        contextButton
    );

    auto *inputRow =
        new QHBoxLayout();

    inputRow->setContentsMargins(
        12,
        0,
        12,
        12
    );

    inputRow->setSpacing(
        8
    );

    inputRow->addWidget(
        m_inputBox,
        1
    );

    inputRow->addWidget(
        m_clearButton
    );

    inputRow->addWidget(
        m_sendButton
    );

    auto *bottomLayout =
        new QVBoxLayout(
            bottomPanel
        );

    bottomLayout->setContentsMargins(
        0,
        0,
        0,
        0
    );

    bottomLayout->setSpacing(
        0
    );

    bottomLayout->addLayout(
        aiRow
    );

    bottomLayout->addLayout(
        statusRow
    );

    bottomLayout->addLayout(
        inputRow
    );

    auto *mainLayout =
        new QVBoxLayout(this);

    mainLayout->setContentsMargins(
        0,
        0,
        0,
        0
    );

    mainLayout->setSpacing(
        0
    );

    mainLayout->addWidget(
        m_webEngineView,
        1
    );

    mainLayout->addWidget(
        bottomPanel
    );

    setLayout(
        mainLayout
    );

    auto *channel =
        new QWebChannel(
            m_webEngineView->page()
        );

    channel->registerObject(
        QStringLiteral("backend"),
        m_backend
    );

    channel->registerObject(
        QStringLiteral("project"),
        m_projectModel
    );

    m_webEngineView
        ->page()
        ->setWebChannel(
            channel
        );
}

void CodeWidget::setupConnections()
{
    connect(
        m_openProjectButton,
        &QPushButton::clicked,
        this,
        &CodeWidget::chooseProjectDirectory
    );

    connect(
        m_refreshModelsButton,
        &QPushButton::clicked,
        this,
        &CodeWidget::refreshModelList
    );

    connect(
        m_endpointEdit,
        &QLineEdit::editingFinished,
        this,
        [this]() {
            saveAiSettings();
            refreshModelList();
        }
    );

    connect(
        m_modelCombo,
        &QComboBox::currentTextChanged,
        this,
        [this](const QString &) {
            saveAiSettings();
        }
    );

    connect(
        m_backend,
        &CodeBackend::codeUpdated,
        this,
        [this](
            const QString &code
        ) {
            m_currentCode =
                code;

            if (!m_currentFile.isEmpty()) {
                m_projectModel
                    ->setEditorBuffer(
                        m_currentFile,
                        code
                    );
            }
        }
    );

    connect(
        m_backend,
        &CodeBackend::messageReceived,
        this,
        [this](
            const QString &text
        ) {
            if (
                text.trimmed().isEmpty()
            ) {
                return;
            }

            appendMessageAsUser(
                text
            );

            sendApiRequest(
                m_contextLevel
            );
        }
    );

    connect(
        m_backend,
        &CodeBackend::reviewRequested,
        this,
        [this](
            const QString &scope
        ) {
            startReview(
                scope
            );
        }
    );

    connect(
        m_backend,
        &CodeBackend::openFileRequested,
        this,
        [this](
            const QString &relativePath
        ) {
            loadProjectFile(
                relativePath
            );
        }
    );

    connect(
        m_sendButton,
        &QPushButton::clicked,
        this,
        [this]() {
            const QString text =
                m_inputBox
                    ->toPlainText()
                    .trimmed();

            if (text.isEmpty()) {
                return;
            }

            if (
                normalizedEndpoint().isEmpty()
            ) {
                showWarningBubble(
                    tr(
                        "Configure an AI endpoint first."
                    )
                );

                return;
            }

            if (
                selectedModel().isEmpty()
            ) {
                showWarningBubble(
                    tr(
                        "Select an AI model first."
                    )
                );

                return;
            }

            m_inputBox->clear();

            appendMessageAsUser(
                text
            );

            sendApiRequest(
                m_contextLevel
            );
        }
    );

    connect(
        m_clearButton,
        &QPushButton::clicked,
        this,
        [this]() {
            m_conversationHistory =
                QJsonArray();

            m_inputBox->clear();

            if (m_isPageLoaded) {
                m_webEngineView
                    ->page()
                    ->runJavaScript(
                        QStringLiteral(
                            "if (window.clearAnnotations)"
                            " window.clearAnnotations();"
                            "if (window.clearChat)"
                            " window.clearChat();"
                        )
                    );
            }

            m_statusLabel->setText(
                tr("Ready")
            );
        }
    );

    connect(
        m_webEngineView,
        &QWebEngineView::loadFinished,
        this,
        [this](bool ok) {
            m_isPageLoaded =
                ok;

            if (!ok) {
                return;
            }

            updateProjectTreeInPage();

            if (
                !m_currentFile.isEmpty()
            ) {
                m_webEngineView
                    ->page()
                    ->runJavaScript(
                        QStringLiteral(
                            "if (window.setCurrentFile)"
                            " window.setCurrentFile(%1);"
                        ).arg(
                            jsonEscape(
                                m_currentFile
                            )
                        )
                    );
            }

            if (
                !m_conversationHistory.isEmpty()
            ) {
                QJsonArray history;

                for (
                    const QJsonValue &value :
                    m_conversationHistory
                ) {
                    const QJsonObject message =
                        value.toObject();

                    const QString role =
                        message.value(
                            QStringLiteral("role")
                        ).toString();

                    const QString content =
                        message.value(
                            QStringLiteral("content")
                        ).toString();

                    if (
                        role !=
                            QStringLiteral("user")
                        && role !=
                            QStringLiteral("assistant")
                    ) {
                        continue;
                    }

                    QJsonObject item;

                    item.insert(
                        QStringLiteral("role"),
                        role
                    );

                    item.insert(
                        QStringLiteral("content"),
                        content
                    );

                    history.append(
                        item
                    );
                }

                m_webEngineView
                    ->page()
                    ->runJavaScript(
                        QStringLiteral(
                            "if (window.restoreChat)"
                            " window.restoreChat(%1);"
                        ).arg(
                            QString::fromUtf8(
                                QJsonDocument(history)
                                    .toJson(
                                        QJsonDocument::Compact
                                    )
                            )
                        )
                    );
            }
        }
    );

    connect(
        m_projectModel,
        &ProjectModel::projectChanged,
        this,
        [this]() {
            const QString directory =
                m_projectModel
                    ->projectDirectory();

            if (directory.isEmpty()) {
                m_projectLabel->setText(
                    tr("No project")
                );

                m_projectLabel->setToolTip(
                    tr(
                        "No project directory selected"
                    )
                );
            } else {
                const QFileInfo info(
                    directory
                );

                m_projectLabel->setText(
                    info.fileName()
                );

                m_projectLabel->setToolTip(
                    directory
                );
            }

            updateProjectTreeInPage();
        }
    );

    connect(
        m_projectModel,
        &ProjectModel::projectTreeChanged,
        this,
        [this](const QVariantList &) {
            updateProjectTreeInPage();
        }
    );

    connect(
        m_agent,
        &CodingAgent::answerReady,
        this,
        [this](
            const QString &answer
        ) {
            if (
                answer.trimmed().isEmpty()
            ) {
                return;
            }

            showAnswer(
                answer
            );

            appendMessageAsAi(
                answer
            );
        }
    );

    connect(
        m_agent,
        &CodingAgent::annotationsReady,
        this,
        [this](
            const QJsonArray &annotations
        ) {
            qDebug().noquote()
                << "[CodeWidget] Received annotations:"
                << QJsonDocument(
                       annotations
                   ).toJson(
                       QJsonDocument::Indented
                   );

            if (
                annotations.isEmpty()
            ) {
                return;
            }

            setAnnotations(
                QString::fromUtf8(
                    QJsonDocument(
                        annotations
                    ).toJson(
                        QJsonDocument::Compact
                    )
                )
            );
        }
    );

    connect(
        m_agent,
        &CodingAgent::editsReady,
        this,
        [this](
            const QString &edits
        ) {
            applyAgentEdits(
                edits
            );
        }
    );

    connect(
        m_agent,
        &CodingAgent::statusChanged,
        this,
        [this](
            const QString &status
        ) {
            m_statusLabel->setText(
                status
            );
        }
    );

    connect(
        m_agent,
        &CodingAgent::requestFinished,
        this,
        [this]() {
            m_sendButton->setEnabled(
                true
            );

            m_clearButton->setEnabled(
                true
            );

            m_sendButton->setText(
                tr("Send")
            );

            m_statusLabel->setText(
                tr("Ready")
            );
        }
    );

    connect(
        m_agent,
        &CodingAgent::requestError,
        this,
        [this](
            const QString &error
        ) {
            qWarning()
                << "[CodeWidget] Agent error:"
                << error;

            showWarningBubble(
                error
            );

            m_sendButton->setEnabled(
                true
            );

            m_clearButton->setEnabled(
                true
            );

            m_sendButton->setText(
                tr("Send")
            );

            m_statusLabel->setText(
                tr("Request error")
            );
        }
    );

    m_webEngineView->load(
        QUrl(
            QStringLiteral(
                "qrc:///widgets/code/code.html"
            )
        )
    );
}

QString CodeWidget::normalizedEndpoint() const
{
    QString endpoint =
        m_endpointEdit
            ? m_endpointEdit
                  ->text()
                  .trimmed()
            : QString();

    while (
        endpoint.endsWith('/')
    ) {
        endpoint.chop(1);
    }

    return endpoint;
}

QString CodeWidget::apiEndpoint() const
{
    return normalizedEndpoint();
}

QString CodeWidget::selectedModel() const
{
    if (!m_modelCombo) {
        return {};
    }

    return m_modelCombo
        ->currentText()
        .trimmed();
}

void CodeWidget::setApiEndpoint(
    const QString &endpoint
)
{
    if (!m_endpointEdit) {
        return;
    }

    m_endpointEdit->setText(
        endpoint.trimmed()
    );
}

void CodeWidget::setSelectedModel(
    const QString &model
)
{
    if (!m_modelCombo) {
        return;
    }

    const QString target =
        model.trimmed();

    if (target.isEmpty()) {
        return;
    }

    const int index =
        m_modelCombo->findText(
            target,
            Qt::MatchExactly
        );

    if (index >= 0) {
        m_modelCombo->setCurrentIndex(
            index
        );
    } else {
        m_modelCombo->setEditText(
            target
        );
    }
}

void CodeWidget::saveAiSettings()
{
    QSettings settings =
        talosSettings();

    settings.setValue(
        kAiEndpointSetting,
        apiEndpoint()
    );

    settings.setValue(
        kAiModelSetting,
        selectedModel()
    );
}

void CodeWidget::restoreAiSettings()
{
    QSettings settings =
        talosSettings();

    const QString endpoint =
        settings.value(
            kAiEndpointSetting,
            kDefaultAiEndpoint
        ).toString();

    const QString model =
        settings.value(
            kAiModelSetting
        ).toString();

    setApiEndpoint(
        endpoint
    );

    if (!model.isEmpty()) {
        setSelectedModel(
            model
        );
    }
}

void CodeWidget::refreshModelList()
{
    const QString endpoint =
        normalizedEndpoint();

    if (endpoint.isEmpty()) {
        m_modelStatusLabel->setText(
            tr("No endpoint")
        );

        return;
    }

    QUrl url(
        endpoint +
        QStringLiteral(
            "/models"
        )
    );

    if (!url.isValid() || url.scheme().isEmpty()) {
        m_modelStatusLabel->setText(
            tr("Invalid URL")
        );

        return;
    }

    if (m_modelReply) {
        m_modelReply->abort();
        m_modelReply->deleteLater();
        m_modelReply = nullptr;
    }

    m_refreshModelsButton->setEnabled(
        false
    );

    m_modelStatusLabel->setText(
        tr("Loading...")
    );

    QNetworkRequest request(url);

    request.setRawHeader(
        "Accept",
        "application/json"
    );

    m_modelReply =
        m_modelNetworkManager->get(
            request
        );

    connect(
        m_modelReply,
        &QNetworkReply::finished,
        this,
        [this]() {
            QNetworkReply *reply =
                m_modelReply;

            m_modelReply =
                nullptr;

            m_refreshModelsButton
                ->setEnabled(
                    true
                );

            if (
                !reply
            ) {
                m_modelStatusLabel->setText(
                    tr("No response")
                );

                return;
            }

            const QByteArray body =
                reply->readAll();

            const auto error =
                reply->error();

            reply->deleteLater();

            if (
                error !=
                QNetworkReply::NoError
            ) {
                qWarning()
                    << "[CodeWidget] Model discovery failed:"
                    << error
                    << body;

                m_modelStatusLabel->setText(
                    tr("Connection failed")
                );

                return;
            }

            QJsonParseError parseError;

            const QJsonDocument document =
                QJsonDocument::fromJson(
                    body,
                    &parseError
                );

            if (
                parseError.error !=
                    QJsonParseError::NoError
                || !document.isObject()
            ) {
                qWarning()
                    << "[CodeWidget] Invalid /models response:"
                    << parseError.errorString();

                m_modelStatusLabel->setText(
                    tr("Invalid response")
                );

                return;
            }

            const QJsonObject root =
                document.object();

            const QJsonArray models =
                root.value(
                    QStringLiteral(
                        "data"
                    )
                ).toArray();

            if (models.isEmpty()) {
                m_modelStatusLabel->setText(
                    tr("No models")
                );

                return;
            }

            populateModels(
                models
            );
        }
    );
}

void CodeWidget::populateModels(
    const QJsonArray &models
)
{
    const QString previous =
        selectedModel();

    m_modelCombo->blockSignals(
        true
    );

    m_modelCombo->clear();

    QStringList modelIds;

    for (
        const QJsonValue &value :
        models
    ) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject object =
            value.toObject();

        QString id =
            object.value(
                QStringLiteral("id")
            ).toString().trimmed();

        if (id.isEmpty()) {
            id =
                object.value(
                    QStringLiteral("name")
                ).toString().trimmed();
        }

        if (id.isEmpty()) {
            continue;
        }

        if (
            !modelIds.contains(
                id
            )
        ) {
            modelIds.append(
                id
            );
        }
    }

    for (
        const QString &id :
        modelIds
    ) {
        m_modelCombo->addItem(
            id
        );
    }

    if (
        !previous.isEmpty()
        && modelIds.contains(
            previous
        )
    ) {
        m_modelCombo->setCurrentText(
            previous
        );
    } else if (
        m_modelCombo->count() > 0
    ) {
        m_modelCombo->setCurrentIndex(
            0
        );
    } else {
        m_modelCombo->setEditText(
            previous
        );
    }

    m_modelCombo->blockSignals(
        false
    );

    saveAiSettings();

    m_modelStatusLabel->setText(
        tr(
            "%1 model%2"
        ).arg(
            modelIds.size()
        ).arg(
            modelIds.size() == 1
                ? QString()
                : QStringLiteral("s")
        )
    );
}

void CodeWidget::chooseProjectDirectory()
{
    const QString existing =
        m_projectModel
            ->projectDirectory();

    const QString startDirectory =
        existing.isEmpty()
            ? QDir::homePath()
            : existing;

    const QString directory =
        QFileDialog::getExistingDirectory(
            this,
            tr("Select Project Directory"),
            startDirectory,
            QFileDialog::ShowDirsOnly
                | QFileDialog::DontResolveSymlinks
        );

    if (directory.isEmpty()) {
        return;
    }

    setProjectDirectory(
        directory
    );
}

void CodeWidget::restoreLastProject()
{
    QSettings settings =
        talosSettings();

    const QString directory =
        settings.value(
            kLastProjectSetting
        ).toString();

    if (
        directory.isEmpty()
        || !QDir(directory).exists()
    ) {
        return;
    }

    setProjectDirectory(
        directory
    );
}

void CodeWidget::setProjectDirectory(
    const QString &path
)
{
    m_projectModel
        ->setProjectDirectory(
            path
        );

    const QString directory =
        m_projectModel
            ->projectDirectory();

    if (directory.isEmpty()) {
        return;
    }

    QSettings settings =
        talosSettings();

    settings.setValue(
        kLastProjectSetting,
        directory
    );

    const QFileInfo info(
        directory
    );

    m_projectLabel->setText(
        info.fileName()
    );

    m_projectLabel->setToolTip(
        directory
    );

    updateProjectTreeInPage();
}

QString CodeWidget::projectDirectory() const
{
    return m_projectModel
        ->projectDirectory();
}

void CodeWidget::setEditorCode(
    const QString &code,
    const QString &language
)
{
    m_currentCode =
        code;

    if (!m_currentFile.isEmpty()) {
        m_projectModel
            ->setEditorBuffer(
                m_currentFile,
                code
            );
    }

    if (!m_isPageLoaded) {
        return;
    }

    if (!m_currentFile.isEmpty()) {
        m_webEngineView
            ->page()
            ->runJavaScript(
                QStringLiteral(
                    "if (window.setCurrentFile)"
                    " window.setCurrentFile(%1);"
                ).arg(
                    jsonEscape(
                        m_currentFile
                    )
                )
            );
    }

    const QString js =
        QStringLiteral(
            "if (typeof window.setEditorCode === "
            "'function') "
            "window.setEditorCode(%1, %2);"
        ).arg(
            jsonEscape(
                code
            ),
            jsonEscape(
                language
            )
        );

    m_webEngineView
        ->page()
        ->runJavaScript(
            js
        );
}

QString CodeWidget::editorCode() const
{
    return m_currentCode;
}

void CodeWidget::setContextLevel(
    LLMPromptBuilder::CodeContextLevel level
)
{
    m_contextLevel =
        level;
}

void CodeWidget::setSelectionMode(
    LLMPromptBuilder::ContextSelectionMode mode
)
{
    m_selectionMode =
        mode;
}

void CodeWidget::setRepoStructure(
    const QString &structure
)
{
    m_repoStructure =
        structure;
}

void CodeWidget::setCurrentFile(
    const QString &filePath
)
{
    loadProjectFile(
        filePath
    );
}

void CodeWidget::setMaxHistoryMessages(
    int maxMessages
)
{
    m_maxRecentMessages =
        qMax(
            1,
            maxMessages
        );
}

int CodeWidget::maxHistoryMessages() const
{
    return m_maxRecentMessages;
}

void CodeWidget::appendMessageAsUser(
    const QString &text
)
{
    QJsonObject message;

    message.insert(
        QStringLiteral("role"),
        QStringLiteral("user")
    );

    message.insert(
        QStringLiteral("content"),
        text
    );

    m_conversationHistory.append(
        message
    );

    if (m_isPageLoaded) {
        m_webEngineView
            ->page()
            ->runJavaScript(
                QStringLiteral(
                    "if (window.appendChatMessage)"
                    " window.appendChatMessage(%1, true);"
                ).arg(
                    jsonEscape(
                        text
                    )
                )
            );
    }
}

void CodeWidget::appendMessageAsAi(
    const QString &text
)
{
    QJsonObject message;

    message.insert(
        QStringLiteral("role"),
        QStringLiteral("assistant")
    );

    message.insert(
        QStringLiteral("content"),
        text
    );

    m_conversationHistory.append(
        message
    );

    if (m_isPageLoaded) {
        m_webEngineView
            ->page()
            ->runJavaScript(
                QStringLiteral(
                    "if (window.appendChatMessage)"
                    " window.appendChatMessage(%1, false);"
                ).arg(
                    jsonEscape(
                        text
                    )
                )
            );
    }
}

void CodeWidget::sendApiRequest(
    LLMPromptBuilder::CodeContextLevel contextLevel
)
{
    const QString endpoint =
        normalizedEndpoint();

    const QString model =
        selectedModel();

    if (endpoint.isEmpty()) {
        showWarningBubble(
            tr(
                "Configure an OpenAI-compatible API endpoint first."
            )
        );

        return;
    }

    if (model.isEmpty()) {
        showWarningBubble(
            tr(
                "Select an AI model first."
            )
        );

        return;
    }

    saveAiSettings();

    m_sendButton->setEnabled(
        false
    );

    m_clearButton->setEnabled(
        false
    );

    m_sendButton->setText(
        tr("Thinking...")
    );

    m_statusLabel->setText(
        tr("Sending request...")
    );

    m_agent->start(
        m_conversationHistory,
        m_currentFile,
        m_currentCode,
        contextLevel,
        endpoint,
        model
    );
}

void CodeWidget::startReview(
    const QString &scope
)
{
    if (
        scope ==
        QStringLiteral("file")
    ) {
        if (
            m_currentFile.isEmpty()
        ) {
            showWarningBubble(
                tr(
                    "Open a project file before reviewing it."
                )
            );

            return;
        }

        appendMessageAsUser(
            QStringLiteral(
                "Review the current file as a senior developer. "
                "Inspect related project code when useful. "
                "Identify meaningful code-quality, maintainability, "
                "correctness, testing, and architectural issues. "
                "Explain why they matter and annotate the relevant lines. "
                "Produce concrete annotations for each significant finding."
            )
        );

        sendApiRequest(
            LLMPromptBuilder::CodeContextLevel::File
        );

        return;
    }

    if (
        scope ==
        QStringLiteral("project")
    ) {
        if (
            m_projectModel
                ->projectDirectory()
                .isEmpty()
        ) {
            showWarningBubble(
                tr(
                    "Set a project directory before reviewing the project."
                )
            );

            return;
        }

        appendMessageAsUser(
            QStringLiteral(
                "Review this project as a senior developer and "
                "pair-programming teacher. Inspect the project structure "
                "and relevant source files using the available tools. "
                "Identify the most important concrete code-quality and "
                "architectural issues. Do not invent issues. Explain why "
                "they matter and annotate the affected source locations. "
                "Produce concrete annotations for each significant finding."
            )
        );

        sendApiRequest(
            LLMPromptBuilder::CodeContextLevel::Project
        );

        return;
    }

    showWarningBubble(
        tr(
            "Unknown review scope: %1"
        ).arg(
            scope
        )
    );
}

void CodeWidget::loadProjectFile(
    const QString &relativePath
)
{
    const QString normalizedPath =
        QDir::fromNativeSeparators(
            relativePath
        );

    const QString code =
        m_projectModel
            ->readFile(
                normalizedPath
            );

    if (code.isNull()) {
        showWarningBubble(
            tr(
                "Could not read %1"
            ).arg(
                normalizedPath
            )
        );

        return;
    }

    m_currentFile =
        normalizedPath;

    m_currentCode =
        code;

    m_projectModel
        ->setEditorBuffer(
            m_currentFile,
            code
        );

    QString language;

    const QString extension =
        QFileInfo(
            m_currentFile
        )
            .suffix()
            .toLower();

    if (
        extension == QStringLiteral("cpp")
        || extension == QStringLiteral("cc")
        || extension == QStringLiteral("cxx")
        || extension == QStringLiteral("h")
        || extension == QStringLiteral("hh")
        || extension == QStringLiteral("hpp")
        || extension == QStringLiteral("hxx")
    ) {
        language =
            QStringLiteral("cpp");
    } else if (
        extension == QStringLiteral("js")
    ) {
        language =
            QStringLiteral("javascript");
    } else if (
        extension == QStringLiteral("ts")
    ) {
        language =
            QStringLiteral("typescript");
    } else if (
        extension == QStringLiteral("py")
    ) {
        language =
            QStringLiteral("python");
    } else if (
        extension == QStringLiteral("rs")
    ) {
        language =
            QStringLiteral("rust");
    } else if (
        extension == QStringLiteral("go")
    ) {
        language =
            QStringLiteral("go");
    } else if (
        extension == QStringLiteral("html")
        || extension == QStringLiteral("htm")
    ) {
        language =
            QStringLiteral("html");
    } else if (
        extension == QStringLiteral("css")
    ) {
        language =
            QStringLiteral("css");
    } else if (
        extension == QStringLiteral("json")
    ) {
        language =
            QStringLiteral("json");
    }

    if (!m_isPageLoaded) {
        return;
    }

    m_webEngineView
        ->page()
        ->runJavaScript(
            QStringLiteral(
                "if (window.setCurrentFile)"
                " window.setCurrentFile(%1);"
            ).arg(
                jsonEscape(
                    m_currentFile
                )
            )
        );

    m_webEngineView
        ->page()
        ->runJavaScript(
            QStringLiteral(
                "if (typeof window.setEditorCode === "
                "'function') "
                "window.setEditorCode(%1, %2);"
            ).arg(
                jsonEscape(
                    m_currentCode
                ),
                jsonEscape(
                    language
                )
            )
        );
}

void CodeWidget::applyAgentEdits(
    const QString &edits
)
{
    if (
        edits.trimmed().isEmpty()
    ) {
        return;
    }

    ParsedResponse parsed;

    parsed.edits =
        edits;

    auto applySearchReplace =
        [this](
            const QString &json
        ) {
            const QString js =
                QStringLiteral(
                    "if (window.applySearchReplace)"
                    " window.applySearchReplace(%1);"
                ).arg(
                    json
                );

            m_webEngineView
                ->page()
                ->runJavaScript(
                    js
                );
        };

    auto applyRangeEdits =
        [this](
            const QString &json
        ) {
            const QString js =
                QStringLiteral(
                    "if (window.applyEdits)"
                    " window.applyEdits(%1);"
                ).arg(
                    json
                );

            m_webEngineView
                ->page()
                ->runJavaScript(
                    js
                );
        };

    auto setFullCode =
        [this](
            const QString &code
        ) {
            const QString js =
                QStringLiteral(
                    "if (typeof window.setEditorCode === "
                    "'function') "
                    "window.setEditorCode(%1, '');"
                ).arg(
                    jsonEscape(
                        code
                    )
                );

            m_webEngineView
                ->page()
                ->runJavaScript(
                    js
                );
        };

    const EditorCommandExecutor::ExecutionResult result =
        m_executor->execute(
            parsed,
            m_currentCode,
            applySearchReplace,
            applyRangeEdits,
            setFullCode
        );

    if (
        !result.success
        && !result.message.isEmpty()
    ) {
        showWarningBubble(
            result.message
        );
    }
}

void CodeWidget::showAnswer(
    const QString &text
)
{
    Q_UNUSED(text);

    if (!m_isPageLoaded) {
        return;
    }

    m_webEngineView
        ->page()
        ->runJavaScript(
            QStringLiteral(
                "if (window.scrollChatToBottom)"
                " window.scrollChatToBottom();"
            )
        );
}

void CodeWidget::showWarningBubble(
    const QString &message
)
{
    QJsonObject annotation;

    annotation.insert(
        QStringLiteral("file"),
        m_currentFile
    );

    annotation.insert(
        QStringLiteral("startLine"),
        1
    );

    annotation.insert(
        QStringLiteral("startColumn"),
        1
    );

    annotation.insert(
        QStringLiteral("endLine"),
        1
    );

    annotation.insert(
        QStringLiteral("endColumn"),
        1
    );

    annotation.insert(
        QStringLiteral("message"),
        message
    );

    annotation.insert(
        QStringLiteral("severity"),
        QStringLiteral("warning")
    );

    QJsonArray annotations;

    annotations.append(
        annotation
    );

    setAnnotations(
        QString::fromUtf8(
            QJsonDocument(
                annotations
            ).toJson(
                QJsonDocument::Compact
            )
        )
    );
}

void CodeWidget::setAnnotations(
    const QString &json
)
{
    if (!m_isPageLoaded) {
        qWarning()
            << "[CodeWidget] Cannot set annotations: "
               "web page is not loaded.";

        return;
    }

    qDebug().noquote()
        << "[CodeWidget] Setting annotations:"
        << json;

    m_webEngineView
        ->page()
        ->runJavaScript(
            QStringLiteral(
                "if (window.setAnnotations)"
                " window.setAnnotations(%1);"
            ).arg(
                json
            )
        );
}

void CodeWidget::updateProjectTreeInPage()
{
    if (!m_isPageLoaded) {
        return;
    }

    const QVariantList tree =
        m_projectModel
            ->projectTree();

    const QJsonDocument document =
        QJsonDocument::fromVariant(
            tree
        );

    const QString json =
        QString::fromUtf8(
            document.toJson(
                QJsonDocument::Compact
            )
        );

    m_webEngineView
        ->page()
        ->runJavaScript(
            QStringLiteral(
                "if (window.setProjectTree)"
                " window.setProjectTree(%1);"
            ).arg(
                json
            )
        );
}

bool CodeWidget::eventFilter(
    QObject *watched,
    QEvent *event
)
{
    if (
        watched == m_inputBox
        && event->type() ==
            QEvent::KeyPress
    ) {
        auto *keyEvent =
            static_cast<QKeyEvent *>(
                event
            );

        if (
            (
                keyEvent->key() ==
                    Qt::Key_Return
                || keyEvent->key() ==
                    Qt::Key_Enter
            )
            && !(
                keyEvent->modifiers()
                & Qt::ShiftModifier
            )
        ) {
            m_sendButton->click();

            return true;
        }
    }

    return QWidget::eventFilter(
        watched,
        event
    );
}