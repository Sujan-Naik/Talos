#include "ResponseProcessor.h"
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

ParsedResponse ResponseProcessor::parseStructuredResponse(const QString &rawResponse)
{
    ParsedResponse result;
    result.hasStructuredFormat = false;

    const QString sanitized = rawResponse.trimmed();

    int editsIdx = sanitized.indexOf(EDITS_TAG);
    int answerIdx = sanitized.indexOf(ANSWER_TAG);
    int annIdx = sanitized.indexOf(ANNOTATIONS_TAG);

    if (editsIdx == -1 && answerIdx == -1 && annIdx == -1) {
        result.answer = sanitized;
        result.hasStructuredFormat = false;
        return result;
    }

    result.hasStructuredFormat = true;

    if (editsIdx != -1) {
        int start = editsIdx + strlen(EDITS_TAG);
        int end = sanitized.length();
        if (answerIdx != -1 && answerIdx > start) end = answerIdx;
        else if (annIdx != -1 && annIdx > start) end = annIdx;
        result.edits = sanitized.mid(start, end - start).trimmed();
    }

    if (answerIdx != -1) {
        int start = answerIdx + strlen(ANSWER_TAG);
        int end = sanitized.length();
        if (annIdx != -1 && annIdx > start) end = annIdx;
        result.answer = sanitized.mid(start, end - start).trimmed();
    }

    if (annIdx != -1) {
        int start = annIdx + strlen(ANNOTATIONS_TAG);
        result.annotations = sanitized.mid(start).trimmed();
    }

    return result;
}

QJsonArray ResponseProcessor::parseSearchReplaceBlocks(const QString &text)
{
    QJsonArray result;

    if (text.isEmpty() || text.trimmed().isEmpty()) {
        return result;
    }

    static const QRegularExpression blockRegex(
        QStringLiteral("<<<<<<<\\s*SEARCH\\s*\\n([\\s\\S]*?)\\n=======\\s*\\n([\\s\\S]*?)\\n>>>>>>>\\s*REPLACE"),
        QRegularExpression::MultilineOption
    );

    QRegularExpressionMatchIterator it = blockRegex.globalMatch(text);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        QString search = m.captured(1);
        QString replace = m.captured(2);

        while (search.endsWith('\n')) search.chop(1);
        while (replace.endsWith('\n')) replace.chop(1);

        QJsonObject obj;
        obj.insert("search", search);
        obj.insert("replace", replace);
        result.append(obj);
    }
    return result;
}

QJsonArray ResponseProcessor::parseAnnotations(const QString &text)
{
    if (text.isEmpty() || text.trimmed() == "[]") {
        return QJsonArray();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (parseError.error == QJsonParseError::NoError && doc.isArray()) {
        return doc.array();
    }
    return QJsonArray();
}

QString ResponseProcessor::extractFirstCodeBlock(const QString &text)
{
    if (text.isEmpty()) return QString();

    static const QRegularExpression codeBlockRegex(
        QStringLiteral("```[^\\n]*\\n([\\s\\S]*?)```")
    );
    QRegularExpressionMatch match = codeBlockRegex.match(text);
    if (match.hasMatch()) {
        QString code = match.captured(1);
        while (code.endsWith('\n')) code.chop(1);
        return code;
    }
    return QString();
}