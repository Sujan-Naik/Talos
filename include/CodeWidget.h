#pragma once

#include <QWidget>
#include <QJsonArray>
#include <QJsonObject>
#include <memory>
#include "EditorCommandExecutor.h"
#include "LLMPromptBuilder.h"
#include "ResponseProcessor.h"

class QWebEngineView;
class QTextEdit;
class QPushButton;
class QLabel;
class QComboBox;
class QSpinBox;
class CodeBackend;
class ApiStreamClient;
class ResponseProcessor;
struct StreamEvent;
struct ParsedResponse;

class CodeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit CodeWidget(QWidget *parent = nullptr);
    ~CodeWidget() override = default;

    void setEditorCode(const QString &code, const QString &language = QString());
    QString editorCode() const;

    // Context management methods (for future use)
    void setContextLevel(LlmPromptBuilder::CodeContextLevel level);
    void setSelectionMode(LlmPromptBuilder::ContextSelectionMode mode);
    void setRepoStructure(const QString &structure);
    void setCurrentFile(const QString &filePath);
    void setCurrentDirectory(const QString &dirPath);
    void setMaxHistoryMessages(int maxMessages);
    int maxHistoryMessages() const;

signals:
    void messageSent(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void sendApiRequest();
    void onStreamEvent(const StreamEvent &event);
    void onRequestFinished();
    void onRequestError(const QString &error);
    void onContextLevelChanged(int index);
    void onSelectionModeChanged(int index);
    void onHistoryLengthChanged(int value);

private:
    void appendMessageAsUser(const QString &text);
    void appendMessageAsAi(const QString &text);
    void showAnswer(const QString &text);
    void showWarningBubble(const QString &message);
    void setAnnotations(const QString &json);
    void setupUi();
    void setupConnections();
    void setupContextControls();
    void updateContextControlsVisibility();
    void updateContextControlsState();

    // UI components
    QWebEngineView *m_webEngineView;
    QTextEdit *m_inputBox;
    QPushButton *m_sendButton;
    QPushButton *m_clearButton;
    QLabel *m_statusLabel;

    // Context controls (future use)
    QComboBox *m_contextLevelCombo;
    QComboBox *m_selectionModeCombo;
    QSpinBox *m_historyLengthSpinBox;
    QPushButton *m_contextSettingsButton;
    QWidget *m_contextControlsPanel;
    QLabel *m_contextLabel;
    QLabel *m_historyLabel;
    QLabel *m_modeLabel;

    // Backend components
    CodeBackend *m_backend;
    std::unique_ptr<ApiStreamClient> m_apiClient;
    std::unique_ptr<EditorCommandExecutor> m_executor;
    LlmPromptBuilder m_promptBuilder;
    ResponseProcessor m_responseProcessor;

    // State
    QString m_currentCode;
    QString m_currentFile;
    QString m_currentDirectory;
    QString m_repoStructure;
    QJsonArray m_conversationHistory;
    QString m_aiStreamAccumulator;
    bool m_isPageLoaded = false;
    bool m_contextControlsVisible = false;

    // Context configuration
    LlmPromptBuilder::CodeContextLevel m_contextLevel = LlmPromptBuilder::CodeContextLevel::File;
    LlmPromptBuilder::ContextSelectionMode m_selectionMode = LlmPromptBuilder::ContextSelectionMode::Manual;
    int m_maxRecentMessages = 20;
};