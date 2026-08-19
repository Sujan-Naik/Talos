#include "EditorCommandExecutor.h"
#include "ResponseProcessor.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

EditorCommandExecutor::ExecutionResult EditorCommandExecutor::execute(
    const ParsedResponse &parsed,
    const QString &currentCode,
    ApplySearchReplaceFunc applySearchReplace,
    ApplyRangeEditsFunc applyRangeEdits,
    SetFullCodeFunc setFullCode
)
{
    ExecutionResult result;
    result.success = true;
    result.message = QString();

    // If no edits section or it's empty, nothing to do
    if (parsed.edits.trimmed().isEmpty()) {
        qDebug() << "[EditorCommandExecutor] No edits section - nothing to apply";
        return result;
    }

    // Parse SEARCH/REPLACE blocks
    ResponseProcessor processor;
    QJsonArray blocks = processor.parseSearchReplaceBlocks(parsed.edits);

    if (!blocks.isEmpty()) {
        QJsonDocument doc(blocks);
        QString json = QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
        qDebug() << "[EditorCommandExecutor] Applying" << blocks.size() << "search/replace blocks";
        applySearchReplace(json);
        result.success = true;
        result.message = QString("Applied %1 search/replace blocks").arg(blocks.size());
        return result;
    }

    // Try full code replacement, but guard against placeholder text
    QString fullCode = processor.extractFirstCodeBlock(parsed.edits);
    if (!fullCode.isEmpty()) {
        // Heuristic: ignore code blocks that are likely "no changes" placeholders
        QString lower = fullCode.toLower();
        if (fullCode.length() > 20 &&
            !lower.contains("no code changes") &&
            !lower.contains("no changes required") &&
            !lower.contains("no changes needed") &&
            !lower.contains("nothing to change")) {
            if (fullCode != currentCode) {
                qDebug() << "[EditorCommandExecutor] Applying full code replacement";
                setFullCode(fullCode);
                result.success = true;
                result.message = "Applied full code replacement";
                return result;
            }
        } else {
            qDebug() << "[EditorCommandExecutor] Ignoring placeholder code block:" << fullCode;
        }
    }

    // No valid edits found, but don't show error if there's explanatory text
    if (!parsed.edits.trimmed().isEmpty() && blocks.isEmpty() && fullCode.isEmpty()) {
        qDebug() << "[EditorCommandExecutor] Edits section contains no valid edit blocks";
        result.success = true;
        result.message = QString();
        return result;
    }

    result.success = false;
    result.message = "No valid edit blocks found in response";
    return result;
}