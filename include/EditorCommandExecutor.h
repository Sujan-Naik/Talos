#pragma once

#include <QString>
#include <QJsonArray>
#include <functional>

struct ParsedResponse;

class EditorCommandExecutor {
public:
    struct ExecutionResult {
        bool success = false;
        QString message;
    };

    using ApplySearchReplaceFunc = std::function<void(const QString&)>;
    using ApplyRangeEditsFunc = std::function<void(const QString&)>;
    using SetFullCodeFunc = std::function<void(const QString&)>;

    ExecutionResult execute(
        const ParsedResponse &parsed,
        const QString &currentCode,
        ApplySearchReplaceFunc applySearchReplace,
        ApplyRangeEditsFunc applyRangeEdits,
        SetFullCodeFunc setFullCode
    );

    ~EditorCommandExecutor() = default;
};