#pragma once

#include "EditorCommandExecutor.h"
#include "LLMPromptBuilder.h"

#include <QJsonArray>
#include <QWidget>

#include <memory>

class QComboBox;
class QLineEdit;
class QPushButton;
class QTextEdit;
class QLabel;
class QNetworkAccessManager;
class QNetworkReply;
class QWebEngineView;

class CodeBackend;
class ProjectModel;
class CodingAgent;

class CodeWidget final : public QWidget
{
    Q_OBJECT

public:
    explicit CodeWidget(QWidget *parent = nullptr);
    ~CodeWidget() override = default;

    void setEditorCode(
        const QString &code,
        const QString &language = QString()
    );

    QString editorCode() const;

    void setProjectDirectory(
        const QString &path
    );

    QString projectDirectory() const;

    void setContextLevel(
        LLMPromptBuilder::CodeContextLevel level
    );

    void setSelectionMode(
        LLMPromptBuilder::ContextSelectionMode mode
    );

    void setRepoStructure(
        const QString &structure
    );

    void setCurrentFile(
        const QString &filePath
    );

    void setMaxHistoryMessages(
        int maxMessages
    );

    int maxHistoryMessages() const;

protected:
    bool eventFilter(
        QObject *watched,
        QEvent *event
    ) override;

private:
    void setupUi();
    void setupConnections();

    void sendApiRequest(
        LLMPromptBuilder::CodeContextLevel contextLevel
    );

    void startReview(
        const QString &scope
    );

    void loadProjectFile(
        const QString &relativePath
    );

    void appendMessageAsUser(
        const QString &text
    );

    void appendMessageAsAi(
        const QString &text
    );

    void showAnswer(
        const QString &text
    );

    void showWarningBubble(
        const QString &message
    );

    void setAnnotations(
        const QString &json
    );

    void updateProjectTreeInPage();

    void applyAgentEdits(
        const QString &edits
    );

    void chooseProjectDirectory();

    void restoreLastProject();

    // --------------------------------------------------------
    // AI provider configuration
    // --------------------------------------------------------

    QString apiEndpoint() const;

    QString selectedModel() const;

    void setApiEndpoint(
        const QString &endpoint
    );

    void setSelectedModel(
        const QString &model
    );

    void refreshModelList();

    void populateModels(
        const QJsonArray &models
    );

    void saveAiSettings();

    void restoreAiSettings();

    QString normalizedEndpoint() const;

private:
    QWebEngineView *m_webEngineView = nullptr;

    QTextEdit *m_inputBox = nullptr;

    QPushButton *m_sendButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QPushButton *m_openProjectButton = nullptr;
    QPushButton *m_refreshModelsButton = nullptr;

    QLineEdit *m_endpointEdit = nullptr;

    QComboBox *m_modelCombo = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_projectLabel = nullptr;
    QLabel *m_modelStatusLabel = nullptr;

    CodeBackend *m_backend = nullptr;

    ProjectModel *m_projectModel = nullptr;

    CodingAgent *m_agent = nullptr;

    QNetworkAccessManager *m_modelNetworkManager = nullptr;

    QNetworkReply *m_modelReply = nullptr;

    std::unique_ptr<EditorCommandExecutor> m_executor;

    QString m_currentCode;
    QString m_currentFile;
    QString m_repoStructure;

    bool m_isPageLoaded = false;

    int m_maxRecentMessages = 20;

    LLMPromptBuilder::CodeContextLevel m_contextLevel =
        LLMPromptBuilder::CodeContextLevel::File;

    LLMPromptBuilder::ContextSelectionMode m_selectionMode =
        LLMPromptBuilder::ContextSelectionMode::Automatic;

    QJsonArray m_conversationHistory;
};