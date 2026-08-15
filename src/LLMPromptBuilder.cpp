#include "../include/LLMPromptBuilder.h"
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

QString LlmPromptBuilder::buildSystemPrompt() const
{
    return QString(SYSTEM_TEMPLATE);
}

QJsonArray LlmPromptBuilder::buildMessages(const PromptContext &ctx) const
{
    QJsonArray messages;

    QJsonObject systemMsg;
    systemMsg.insert("role", "system");
    systemMsg.insert("content", SYSTEM_TEMPLATE);
    messages.append(systemMsg);

    QJsonArray historyMessages = buildHistoryMessages(ctx);
    for (const QJsonValue &msg : historyMessages) {
        messages.append(msg);
    }

    QJsonArray contextMessages = buildCodeContextMessages(ctx);
    for (const QJsonValue &msg : contextMessages) {
        messages.append(msg);
    }

    return messages;
}

QJsonArray LlmPromptBuilder::buildHistoryMessages(const PromptContext &ctx) const
{
    QJsonArray result;

    if (!ctx.recentHistory.isEmpty()) {
        return ctx.recentHistory;
    }

    if (ctx.fullHistory.isEmpty()) {
        return result;
    }

    const int totalMessages = ctx.fullHistory.size();
    const int startIdx = qMax(0, totalMessages - ctx.maxRecentMessages);

    for (int i = startIdx; i < totalMessages; ++i) {
        result.append(ctx.fullHistory.at(i));
    }

    return result;
}

QJsonArray LlmPromptBuilder::buildCodeContextMessages(const PromptContext &ctx) const
{
    QJsonArray result;

    if (!ctx.currentCode.isEmpty()) {
        QJsonObject contextMsg;
        contextMsg.insert("role", "system");
        contextMsg.insert("name", "file_context");
        contextMsg.insert("content", buildFileContextMessage(ctx));
        result.append(contextMsg);
    }

    return result;
}

QString LlmPromptBuilder::buildFileContextMessage(const PromptContext &ctx) const
{
    QString content = "CURRENT EDITOR CODE:\n";
    if (!ctx.currentFile.isEmpty()) {
        content += QString("File: %1\n").arg(ctx.currentFile);
    }
    content += QString("```\n%1\n```").arg(ctx.currentCode);
    return content;
}

LlmPromptBuilder::CodeContextLevel LlmPromptBuilder::suggestContextLevel(const QString &userMessage) const
{
    Q_UNUSED(userMessage);
    return CodeContextLevel::File;
}