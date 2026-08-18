#include "CodingAgent.h"

#include "ProjectModel.h"
#include "ResponseProcessor.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrl>

namespace
{
const QString kToolRequestTag =
    QStringLiteral(
        "<<<TOOL_REQUEST>>>"
    );

const QString kToolRequestEndTag =
    QStringLiteral(
        "<<<END_TOOL_REQUEST>>>"
    );
}

CodingAgent::CodingAgent(
    ProjectModel *projectModel,
    QObject *parent
)
    : QObject(parent)
    , m_projectModel(projectModel)
    , m_apiClient(
        std::make_unique<ApiStreamClient>(
            new QNetworkAccessManager(this),
            this
        )
    )
{
    connect(
        m_apiClient.get(),
        &ApiStreamClient::eventReceived,
        this,
        [this](
            const StreamEvent &event
        ) {
            if (
                event.type !=
                StreamEventType::TextDelta
            ) {
                return;
            }

            if (event.data.isEmpty()) {
                return;
            }

            m_streamAccumulator +=
                event.data;
        }
    );

    connect(
        m_apiClient.get(),
        &ApiStreamClient::requestFinished,
        this,
        &CodingAgent::handleCompletedModelResponse
    );

    connect(
        m_apiClient.get(),
        &ApiStreamClient::requestError,
        this,
        &CodingAgent::requestError
    );
}

void CodingAgent::start(
    const QJsonArray &history,
    const QString &currentFile,
    const QString &currentCode,
    LLMPromptBuilder::CodeContextLevel contextLevel,
    const QString &endpointUrl,
    const QString &model
)
{
    abort();

    m_currentFile =
        currentFile;

    m_currentCode =
        currentCode;

    m_endpointUrl =
        normalizeEndpointUrl(
            endpointUrl
        );

    m_model =
        model.trimmed();

    if (m_endpointUrl.isEmpty()) {
        emit requestError(
            QStringLiteral(
                "AI endpoint is empty."
            )
        );

        return;
    }

    if (m_model.isEmpty()) {
        emit requestError(
            QStringLiteral(
                "No AI model is selected."
            )
        );

        return;
    }

    m_toolRounds = 0;
    m_streamAccumulator.clear();

    LLMPromptBuilder::PromptContext context;

    context.fullHistory =
        history;

    context.maxRecentMessages =
        20;

    context.currentCode =
        currentCode;

    context.currentFile =
        currentFile;

    context.currentDirectory =
        m_projectModel
            ? m_projectModel->projectDirectory()
            : QString();

    context.projectDirectory =
        m_projectModel
            ? m_projectModel->projectDirectory()
            : QString();

    context.projectFiles =
        m_projectModel
            ? m_projectModel->files()
            : QStringList();

    context.contextLevel =
        contextLevel;

    context.model =
        m_model;

    context.temperature =
        0.15;

    m_messages =
        m_promptBuilder.buildMessages(
            context
        );

    qDebug()
        << "[CodingAgent] Endpoint:"
        << m_endpointUrl;

    qDebug()
        << "[CodingAgent] Model:"
        << m_model;

    qDebug().noquote()
        << "[CodingAgent] Request messages:"
        << QJsonDocument(m_messages)
               .toJson(
                   QJsonDocument::Indented
               );

    sendCurrentMessages();
}

void CodingAgent::abort()
{
    if (m_apiClient) {
        m_apiClient->abortRequest();
    }

    m_streamAccumulator.clear();
}

QString CodingAgent::normalizeEndpointUrl(
    const QString &endpoint
) const
{
    QString result =
        endpoint.trimmed();

    while (
        result.endsWith('/')
    ) {
        result.chop(1);
    }

    return result;
}

void CodingAgent::sendCurrentMessages()
{
    if (m_messages.isEmpty()) {
        emit requestError(
            QStringLiteral(
                "Cannot start agent: no messages."
            )
        );

        return;
    }

    if (m_endpointUrl.isEmpty()) {
        emit requestError(
            QStringLiteral(
                "AI endpoint is not configured."
            )
        );

        return;
    }

    if (m_model.isEmpty()) {
        emit requestError(
            QStringLiteral(
                "AI model is not configured."
            )
        );

        return;
    }

    m_streamAccumulator.clear();

    ApiStreamClient::RequestParams params;

    params.url =
        m_endpointUrl +
        QStringLiteral(
            "/chat/completions"
        );

    params.messages =
        m_messages;

    params.model =
        m_model;

    params.temperature =
        0.15;

    params.timeoutMs =
        120000;

    emit statusChanged(
        QStringLiteral(
            "Thinking..."
        )
    );

    m_apiClient->sendRequest(
        params
    );
}

bool CodingAgent::parseToolRequest(
    const QString &text,
    ToolRequest &request
) const
{
    // Preferred tagged format.
    const int taggedStart =
        text.indexOf(
            kToolRequestTag
        );

    if (taggedStart >= 0) {
        const int jsonStart =
            taggedStart +
            kToolRequestTag.length();

        const int taggedEnd =
            text.indexOf(
                kToolRequestEndTag,
                jsonStart
            );

        if (taggedEnd >= 0) {
            const QString jsonText =
                text.mid(
                    jsonStart,
                    taggedEnd - jsonStart
                ).trimmed();

            QJsonParseError parseError;

            const QJsonDocument document =
                QJsonDocument::fromJson(
                    jsonText.toUtf8(),
                    &parseError
                );

            if (
                parseError.error ==
                    QJsonParseError::NoError
                && document.isObject()
            ) {
                const QJsonObject object =
                    document.object();

                request.name =
                    object.value(
                        QStringLiteral("name")
                    )
                        .toString()
                        .trimmed();

                request.arguments =
                    object.value(
                        QStringLiteral("arguments")
                    ).toObject();

                return !request.name.isEmpty();
            }

            qWarning()
                << "[CodingAgent] Invalid tagged tool request:"
                << parseError.errorString();

            return false;
        }
    }

    // Tolerate:
    //
    // TOOL_REQUEST
    // {"name":"read_file",...}
    //
    const QRegularExpression looseToolPattern(
        QStringLiteral(
            R"(^\s*(?:```(?:json)?\s*)?TOOL_REQUEST\s*(?:```)?\s*(\{[\s\S]*\})\s*$)"
        ),
        QRegularExpression::MultilineOption
    );

    const QRegularExpressionMatch looseMatch =
        looseToolPattern.match(
            text
        );

    if (looseMatch.hasMatch()) {
        const QString jsonText =
            looseMatch.captured(1).trimmed();

        QJsonParseError parseError;

        const QJsonDocument document =
            QJsonDocument::fromJson(
                jsonText.toUtf8(),
                &parseError
            );

        if (
            parseError.error ==
                QJsonParseError::NoError
            && document.isObject()
        ) {
            const QJsonObject object =
                document.object();

            request.name =
                object.value(
                    QStringLiteral("name")
                )
                    .toString()
                    .trimmed();

            request.arguments =
                object.value(
                    QStringLiteral("arguments")
                ).toObject();

            return !request.name.isEmpty();
        }
    }

    // Conservative last-resort JSON detection.
    const int jsonStart =
        text.indexOf(
            QStringLiteral("{")
        );

    const int jsonEnd =
        text.lastIndexOf(
            QStringLiteral("}")
        );

    if (
        jsonStart >= 0
        && jsonEnd > jsonStart
    ) {
        const QString candidate =
            text.mid(
                jsonStart,
                jsonEnd - jsonStart + 1
            ).trimmed();

        QJsonParseError parseError;

        const QJsonDocument document =
            QJsonDocument::fromJson(
                candidate.toUtf8(),
                &parseError
            );

        if (
            parseError.error ==
                QJsonParseError::NoError
            && document.isObject()
        ) {
            const QJsonObject object =
                document.object();

            const QString name =
                object.value(
                    QStringLiteral("name")
                )
                    .toString()
                    .trimmed();

            if (!name.isEmpty()) {
                request.name =
                    name;

                request.arguments =
                    object.value(
                        QStringLiteral("arguments")
                    ).toObject();

                return true;
            }
        }
    }

    return false;
}

void CodingAgent::handleCompletedModelResponse()
{
    const QString response =
        m_streamAccumulator.trimmed();

    qDebug().noquote()
        << "[CodingAgent] Complete model response:\n"
        << response;

    if (response.isEmpty()) {
        emit requestError(
            QStringLiteral(
                "The model returned an empty response."
            )
        );

        return;
    }

    ToolRequest toolRequest;

    if (
        parseToolRequest(
            response,
            toolRequest
        )
    ) {
        if (
            m_toolRounds >=
            m_maxToolRounds
        ) {
            emit requestError(
                QStringLiteral(
                    "Agent stopped after too many tool calls."
                )
            );

            return;
        }

        ++m_toolRounds;

        emit statusChanged(
            QStringLiteral(
                "Inspecting project: %1"
            ).arg(
                toolRequest.name
            )
        );

        const QString toolResult =
            executeTool(
                toolRequest
            );

        appendMessage(
            QStringLiteral(
                "user"
            ),
            QStringLiteral(
                "TOOL RESULT\n"
                "Tool: %1\n"
                "Arguments:\n"
                "%2\n\n"
                "Result:\n"
                "%3"
            ).arg(
                toolRequest.name,
                QString::fromUtf8(
                    QJsonDocument(
                        toolRequest.arguments
                    ).toJson(
                        QJsonDocument::Compact
                    )
                ),
                toolResult
            ),
            QStringLiteral(
                "tool_result"
            )
        );

        sendCurrentMessages();

        return;
    }

    ResponseProcessor processor;

    const ParsedResponse parsed =
        processor.parseStructuredResponse(
            response
        );

    if (
        !parsed.answer.isEmpty()
    ) {
        emit answerReady(
            parsed.answer
        );
    } else {
        emit answerReady(
            response
        );
    }

    if (
        !parsed.annotations.isEmpty()
    ) {
        const QJsonArray annotations =
            processor.parseAnnotations(
                parsed.annotations
            );

        if (!annotations.isEmpty()) {
            emit annotationsReady(
                annotations
            );
        } else {
            qWarning()
                << "[CodingAgent] Annotation section "
                   "could not be parsed.";
        }
    }

    if (
        !parsed.edits.isEmpty()
    ) {
        emit editsReady(
            parsed.edits
        );
    }

    emit statusChanged(
        QStringLiteral(
            "Ready"
        )
    );

    emit requestFinished();
}

QString CodingAgent::executeTool(
    const ToolRequest &request
)
{
    if (!m_projectModel) {
        return QStringLiteral(
            "ERROR: Project model is unavailable."
        );
    }

    if (
        request.name ==
        QStringLiteral(
            "list_files"
        )
    ) {
        return executeListFiles(
            request.arguments
        );
    }

    if (
        request.name ==
        QStringLiteral(
            "resolve_file"
        )
    ) {
        return executeResolveFile(
            request.arguments
        );
    }

    if (
        request.name ==
        QStringLiteral(
            "read_file"
        )
    ) {
        return executeReadFile(
            request.arguments
        );
    }

    if (
        request.name ==
        QStringLiteral(
            "read_file_range"
        )
    ) {
        return executeReadFileRange(
            request.arguments
        );
    }

    if (
        request.name ==
        QStringLiteral(
            "search"
        )
    ) {
        return executeSearch(
            request.arguments
        );
    }

    return QStringLiteral(
        "ERROR: Unknown tool '%1'. "
        "Available tools: list_files, resolve_file, read_file, "
        "read_file_range, search."
    ).arg(
        request.name
    );
}

QString CodingAgent::executeListFiles(
    const QJsonObject &arguments
)
{
    Q_UNUSED(arguments);

    QJsonArray files;

    for (
        const QString &file :
        m_projectModel->files()
    ) {
        files.append(
            file
        );
    }

    QJsonObject result;

    result.insert(
        QStringLiteral("files"),
        files
    );

    return QString::fromUtf8(
        QJsonDocument(result)
            .toJson(
                QJsonDocument::Indented
            )
    );
}

QString CodingAgent::executeResolveFile(
    const QJsonObject &arguments
)
{
    const QString query =
        arguments.value(
            QStringLiteral("query")
        )
            .toString()
            .trimmed();

    const QString relativeTo =
        arguments.value(
            QStringLiteral("relativeTo")
        )
            .toString()
            .trimmed();

    if (query.isEmpty()) {
        return QStringLiteral(
            "ERROR: resolve_file requires 'query'."
        );
    }

    const QVariantMap resolution =
        m_projectModel->resolveFile(
            query,
            relativeTo
        );

    return QString::fromUtf8(
        QJsonDocument::fromVariant(
            resolution
        ).toJson(
            QJsonDocument::Indented
        )
    );
}

QString CodingAgent::executeReadFile(
    const QJsonObject &arguments
)
{
    const QString requestedPath =
        arguments.value(
            QStringLiteral("path")
        )
            .toString()
            .trimmed();

    if (requestedPath.isEmpty()) {
        return QStringLiteral(
            "ERROR: read_file requires 'path'."
        );
    }

    const QVariantMap resolution =
        m_projectModel->resolveFile(
            requestedPath,
            m_currentFile
        );

    const QString status =
        resolution.value(
            QStringLiteral("status")
        ).toString();

    if (
        status ==
        QStringLiteral("ambiguous")
    ) {
        return QStringLiteral(
            "AMBIGUOUS FILE REQUEST\n"
            "Query: %1\n"
            "Candidates:\n%2\n"
            "Use one of the exact candidate paths with read_file."
        ).arg(
            requestedPath,
            QString::fromUtf8(
                QJsonDocument::fromVariant(
                    resolution.value(
                        QStringLiteral("candidates")
                    )
                ).toJson(
                    QJsonDocument::Indented
                )
            )
        );
    }

    if (
        status !=
        QStringLiteral("resolved")
    ) {
        return QStringLiteral(
            "FILE NOT FOUND\n"
            "Query: %1\n"
            "Try resolve_file with the query and "
            "relativeTo=%2."
        ).arg(
            requestedPath,
            m_currentFile
        );
    }

    const QString resolvedPath =
        resolution.value(
            QStringLiteral("path")
        ).toString();

    const QString content =
        m_projectModel->readFile(
            resolvedPath
        );

    if (content.isNull()) {
        return QStringLiteral(
            "ERROR: File '%1' resolved from '%2' "
            "but could not be read."
        ).arg(
            resolvedPath,
            requestedPath
        );
    }

    return QStringLiteral(
        "FILE: %1\n"
        "REQUESTED AS: %2\n"
        "CONTENT:\n"
        "```\n"
        "%3\n"
        "```"
    ).arg(
        resolvedPath,
        requestedPath,
        content
    );
}

QString CodingAgent::executeReadFileRange(
    const QJsonObject &arguments
)
{
    const QString requestedPath =
        arguments.value(
            QStringLiteral("path")
        )
            .toString()
            .trimmed();

    const int startLine =
        arguments.value(
            QStringLiteral("startLine")
        ).toInt(
            1
        );

    const int endLine =
        arguments.value(
            QStringLiteral("endLine")
        ).toInt(
            startLine + 80
        );

    if (
        requestedPath.isEmpty()
    ) {
        return QStringLiteral(
            "ERROR: read_file_range requires 'path'."
        );
    }

    const QVariantMap resolution =
        m_projectModel->resolveFile(
            requestedPath,
            m_currentFile
        );

    const QString status =
        resolution.value(
            QStringLiteral("status")
        ).toString();

    if (
        status ==
        QStringLiteral("ambiguous")
    ) {
        return QStringLiteral(
            "AMBIGUOUS FILE REQUEST\n"
            "Query: %1\n"
            "Candidates:\n%2"
        ).arg(
            requestedPath,
            QString::fromUtf8(
                QJsonDocument::fromVariant(
                    resolution.value(
                        QStringLiteral("candidates")
                    )
                ).toJson(
                    QJsonDocument::Indented
                )
            )
        );
    }

    if (
        status !=
        QStringLiteral("resolved")
    ) {
        return QStringLiteral(
            "FILE NOT FOUND\n"
            "Query: %1"
        ).arg(
            requestedPath
        );
    }

    const QString resolvedPath =
        resolution.value(
            QStringLiteral("path")
        ).toString();

    const QString content =
        m_projectModel->readFileRange(
            resolvedPath,
            startLine,
            endLine
        );

    return QStringLiteral(
        "FILE: %1\n"
        "REQUESTED AS: %2\n"
        "LINES: %3-%4\n"
        "CONTENT:\n"
        "```\n"
        "%5\n"
        "```"
    ).arg(
        resolvedPath,
        requestedPath,
        QString::number(startLine),
        QString::number(endLine),
        content
    );
}

QString CodingAgent::executeSearch(
    const QJsonObject &arguments
)
{
    const QString query =
        arguments.value(
            QStringLiteral("query")
        )
            .toString()
            .trimmed();

    const int maxResults =
        arguments.value(
            QStringLiteral("maxResults")
        )
            .toInt(
                50
            );

    if (query.isEmpty()) {
        return QStringLiteral(
            "ERROR: search requires 'query'."
        );
    }

    const QVariantList results =
        m_projectModel->search(
            query,
            maxResults
        );

    QJsonArray resultArray;

    for (
        const QVariant &result :
        results
    ) {
        resultArray.append(
            QJsonObject::fromVariantMap(
                result.toMap()
            )
        );
    }

    QJsonObject result;

    result.insert(
        QStringLiteral("query"),
        query
    );

    result.insert(
        QStringLiteral("results"),
        resultArray
    );

    return QString::fromUtf8(
        QJsonDocument(result)
            .toJson(
                QJsonDocument::Indented
            )
    );
}

void CodingAgent::appendMessage(
    const QString &role,
    const QString &content,
    const QString &name
)
{
    QJsonObject message;

    message.insert(
        QStringLiteral("role"),
        role
    );

    message.insert(
        QStringLiteral("content"),
        content
    );

    if (!name.isEmpty()) {
        message.insert(
            QStringLiteral("name"),
            name
        );
    }

    m_messages.append(
        message
    );
}