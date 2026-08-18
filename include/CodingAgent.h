#pragma once

#include "ApiStreamClient.h"
#include "LLMPromptBuilder.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <memory>

class ProjectModel;

class CodingAgent final : public QObject
{
    Q_OBJECT

public:
    explicit CodingAgent(
        ProjectModel *projectModel,
        QObject *parent = nullptr
    );

    void start(
        const QJsonArray &history,
        const QString &currentFile,
        const QString &currentCode,
        LLMPromptBuilder::CodeContextLevel contextLevel,
        const QString &endpointUrl,
        const QString &model
    );

    void abort();

signals:
    void answerReady(const QString &answer);
    void annotationsReady(const QJsonArray &annotations);
    void editsReady(const QString &edits);

    void statusChanged(const QString &status);
    void requestFinished();
    void requestError(const QString &error);

private:
    struct ToolRequest
    {
        QString name;
        QJsonObject arguments;
    };

    void sendCurrentMessages();

    void handleCompletedModelResponse();

    bool parseToolRequest(
        const QString &text,
        ToolRequest &request
    ) const;

    QString executeTool(
        const ToolRequest &request
    );

    QString executeListFiles(
        const QJsonObject &arguments
    );

    QString executeResolveFile(
        const QJsonObject &arguments
    );

    QString executeReadFile(
        const QJsonObject &arguments
    );

    QString executeReadFileRange(
        const QJsonObject &arguments
    );

    QString executeSearch(
        const QJsonObject &arguments
    );

    void appendMessage(
        const QString &role,
        const QString &content,
        const QString &name = QString()
    );

    QString normalizeEndpointUrl(
        const QString &endpoint
    ) const;

private:
    ProjectModel *m_projectModel = nullptr;

    LLMPromptBuilder m_promptBuilder;

    std::unique_ptr<ApiStreamClient> m_apiClient;

    QJsonArray m_messages;

    QString m_streamAccumulator;

    QString m_currentFile;
    QString m_currentCode;

    QString m_endpointUrl;
    QString m_model;

    int m_toolRounds = 0;
    int m_maxToolRounds = 12;
};