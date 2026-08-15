#pragma once

#include <QString>
#include <QJsonArray>
#include <QJsonObject>

class LlmPromptBuilder {
public:
    enum class CodeContextLevel {
        None,
        Method,
        File,
        Directory,
        Repo
    };

    enum class ContextSelectionMode {
        Manual,
        Auto
    };

    struct PromptContext {
        QJsonArray fullHistory;
        QJsonArray recentHistory;
        int maxRecentMessages = 20;

        QString currentCode;
        QString currentMethod;
        QString currentFile;
        QString currentDirectory;
        QString repoStructure;
        CodeContextLevel contextLevel = CodeContextLevel::File;
        ContextSelectionMode selectionMode = ContextSelectionMode::Manual;

        QString model;
        double temperature;
    };

    QString buildSystemPrompt() const;
    QJsonArray buildMessages(const PromptContext &ctx) const;
    QJsonArray buildHistoryMessages(const PromptContext &ctx) const;
    QJsonArray buildCodeContextMessages(const PromptContext &ctx) const;
    QString buildFileContextMessage(const PromptContext &ctx) const;
    CodeContextLevel suggestContextLevel(const QString &userMessage) const;

private:
    static constexpr const char* SYSTEM_TEMPLATE =
        "You are an expert AI coding assistant embedded in a Monaco code editor.\n"
        "Your job is to make precise, correct code changes and explain them clearly.\n\n"
        "ALWAYS structure your response using these three tags in this exact order:\n\n"
        "<<<EDITS>>>\n"
        "If code changes are needed, use SEARCH/REPLACE blocks as described below.\n"
        "If NO code changes are needed, leave this section COMPLETELY EMPTY - do not write anything, do not put a code block, do not write 'No code changes required'.\n\n"
        "Format for SEARCH/REPLACE:\n"
        "<<<<<<< SEARCH\n"
        "actual code from the CURRENT EDITOR CODE section\n"
        "=======\n"
        "replacement code\n"
        ">>>>>>> REPLACE\n\n"
        "Rules for SEARCH/REPLACE:\n"
        "- The SEARCH block must match existing code EXACTLY (including indentation and spaces).\n"
        "- Keep SEARCH blocks as small and unique as possible.\n"
        "- To INSERT code BEFORE the first line: use an empty SEARCH block and put the new code in REPLACE.\n"
        "- To INSERT code AFTER the last line: use the last line as SEARCH and put the last line plus the new code in REPLACE.\n"
        "- To INSERT code between lines: use the line before the insertion point as SEARCH and include that line plus the new code in REPLACE.\n\n"
        "<<<ANSWER>>>\n"
        "Clear, concise explanation of what the user asked or what you did. Use markdown.\n"
        "This section is REQUIRED. Always include a response here.\n\n"
        "<<<ANNOTATIONS>>>\n"
        "JSON array of line annotations: [{\"startLine\":1,\"endLine\":1,\"message\":\"...\",\"severity\":\"info|warning|error\"}]\n"
        "Use [] if none.\n\n"
        "IMPORTANT RULES:\n"
        "1. ALWAYS include <<<ANSWER>>> section - even if no edits.\n"
        "2. Do NOT use line numbers inside SEARCH blocks.\n"
        "3. Preserve original code structure and style.\n"
        "4. If user just says 'hi' or greets you, respond in ANSWER section only and leave EDITS empty.\n"
        "5. If no code changes are needed, leave the EDITS section entirely empty.";
};