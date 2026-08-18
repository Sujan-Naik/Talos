#include "ResponseProcessor.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

ParsedResponse ResponseProcessor::parseStructuredResponse(
    const QString &rawResponse
)
{
    ParsedResponse result;
    result.hasStructuredFormat = false;

    const QString sanitized =
        rawResponse.trimmed();

    const int editsIdx =
        sanitized.indexOf(
            EDITS_TAG
        );

    const int answerIdx =
        sanitized.indexOf(
            ANSWER_TAG
        );

    const int annotationsIdx =
        sanitized.indexOf(
            ANNOTATIONS_TAG
        );

    if (
        editsIdx == -1
        && answerIdx == -1
        && annotationsIdx == -1
    ) {
        result.answer =
            sanitized;

        result.hasStructuredFormat =
            false;

        return result;
    }

    result.hasStructuredFormat =
        true;

    // --------------------------------------------------------
    // EDItS
    // --------------------------------------------------------

    if (editsIdx != -1) {
        const int start =
            editsIdx +
            QString::fromLatin1(
                EDITS_TAG
            ).length();

        int end =
            sanitized.length();

        if (
            answerIdx != -1
            && answerIdx > start
        ) {
            end =
                qMin(
                    end,
                    answerIdx
                );
        }

        if (
            annotationsIdx != -1
            && annotationsIdx > start
        ) {
            end =
                qMin(
                    end,
                    annotationsIdx
                );
        }

        result.edits =
            sanitized
                .mid(
                    start,
                    end - start
                )
                .trimmed();
    }

    // --------------------------------------------------------
    // ANSWER
    // --------------------------------------------------------

    if (answerIdx != -1) {
        const int start =
            answerIdx +
            QString::fromLatin1(
                ANSWER_TAG
            ).length();

        int end =
            sanitized.length();

        if (
            annotationsIdx != -1
            && annotationsIdx > start
        ) {
            end =
                qMin(
                    end,
                    annotationsIdx
                );
        }

        if (
            editsIdx != -1
            && editsIdx > start
        ) {
            end =
                qMin(
                    end,
                    editsIdx
                );
        }

        result.answer =
            sanitized
                .mid(
                    start,
                    end - start
                )
                .trimmed();
    }

    // --------------------------------------------------------
    // ANNOTATIONS
    // --------------------------------------------------------

    if (annotationsIdx != -1) {
        const int start =
            annotationsIdx +
            QString::fromLatin1(
                ANNOTATIONS_TAG
            ).length();

        int end =
            sanitized.length();

        if (
            editsIdx != -1
            && editsIdx > start
        ) {
            end =
                qMin(
                    end,
                    editsIdx
                );
        }

        result.annotations =
            sanitized
                .mid(
                    start,
                    end - start
                )
                .trimmed();
    }

    return result;
}

QJsonArray ResponseProcessor::parseSearchReplaceBlocks(
    const QString &text
)
{
    QJsonArray result;

    if (
        text.isEmpty()
        || text.trimmed().isEmpty()
    ) {
        return result;
    }

    static const QRegularExpression blockRegex(
        QStringLiteral(
            "<<<<<<<\\s*SEARCH\\s*\\n"
            "([\\s\\S]*?)\\n"
            "=======\\s*\\n"
            "([\\s\\S]*?)\\n"
            ">>>>>>>\\s*REPLACE"
        ),
        QRegularExpression::MultilineOption
    );

    QRegularExpressionMatchIterator it =
        blockRegex.globalMatch(
            text
        );

    while (it.hasNext()) {
        const QRegularExpressionMatch match =
            it.next();

        QString search =
            match.captured(1);

        QString replace =
            match.captured(2);

        while (search.endsWith('\n')) {
            search.chop(1);
        }

        while (replace.endsWith('\n')) {
            replace.chop(1);
        }

        QJsonObject object;

        object.insert(
            QStringLiteral("search"),
            search
        );

        object.insert(
            QStringLiteral("replace"),
            replace
        );

        result.append(
            object
        );
    }

    return result;
}

QJsonArray ResponseProcessor::parseAnnotations(
    const QString &text
)
{
    if (
        text.isEmpty()
        || text.trimmed() == QStringLiteral("[]")
    ) {
        return QJsonArray();
    }

    QString jsonText =
        text.trimmed();

    // Be tolerant of models wrapping the array in a
    // Markdown JSON fence.
    if (
        jsonText.startsWith(
            QStringLiteral("```")
        )
    ) {
        const int firstNewline =
            jsonText.indexOf('\n');

        const int lastFence =
            jsonText.lastIndexOf(
                QStringLiteral("```")
            );

        if (
            firstNewline != -1
            && lastFence > firstNewline
        ) {
            jsonText =
                jsonText
                    .mid(
                        firstNewline + 1,
                        lastFence -
                            firstNewline -
                            1
                    )
                    .trimmed();
        }
    }

    // If the model included additional prose around the JSON,
    // extract the outermost JSON array.
    const int arrayStart =
        jsonText.indexOf('[');

    const int arrayEnd =
        jsonText.lastIndexOf(']');

    if (
        arrayStart != -1
        && arrayEnd > arrayStart
    ) {
        jsonText =
            jsonText
                .mid(
                    arrayStart,
                    arrayEnd -
                        arrayStart +
                        1
                )
                .trimmed();
    }

    QJsonParseError parseError;

    const QJsonDocument document =
        QJsonDocument::fromJson(
            jsonText.toUtf8(),
            &parseError
        );

    if (
        parseError.error ==
            QJsonParseError::NoError
        && document.isArray()
    ) {
        return document.array();
    }

    qWarning()
        << "[ResponseProcessor] Failed to parse annotations:"
        << parseError.errorString();

    qWarning().noquote()
        << "[ResponseProcessor] Annotation text:"
        << jsonText;

    return QJsonArray();
}

QString ResponseProcessor::extractFirstCodeBlock(
    const QString &text
)
{
    if (text.isEmpty()) {
        return QString();
    }

    static const QRegularExpression codeBlockRegex(
        QStringLiteral(
            "```[^\\n]*\\n([\\s\\S]*?)```"
        )
    );

    const QRegularExpressionMatch match =
        codeBlockRegex.match(
            text
        );

    if (!match.hasMatch()) {
        return QString();
    }

    QString code =
        match.captured(1);

    while (code.endsWith('\n')) {
        code.chop(1);
    }

    return code;
}