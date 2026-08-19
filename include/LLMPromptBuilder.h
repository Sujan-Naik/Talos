#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>

class LLMPromptBuilder
{
public:
    enum class CodeContextLevel {
        Selection,
        File,
        Project
    };

    enum class ContextSelectionMode {
        Current,
        Selection,
        Automatic
    };

    struct PromptContext
    {
        QJsonArray fullHistory;
        QJsonArray recentHistory;
        int maxRecentMessages = 20;

        QString currentCode;
        QString currentFile;
        QString currentDirectory;

        QString projectDirectory;
        QStringList projectFiles;

        CodeContextLevel contextLevel =
            CodeContextLevel::Project;

        QString model;
        double temperature = 0.15;
    };

    QString buildSystemPrompt() const;

    QJsonArray buildMessages(
        const PromptContext &ctx
    ) const;

    CodeContextLevel suggestContextLevel(
        const QString &userMessage
    ) const;

private:
    QJsonArray buildHistoryMessages(
        const PromptContext &ctx
    ) const;

    QJsonArray buildCodeContextMessages(
        const PromptContext &ctx
    ) const;

    QString buildProjectContextMessage(
        const PromptContext &ctx
    ) const;

    QString buildFileContextMessage(
        const PromptContext &ctx
    ) const;
};