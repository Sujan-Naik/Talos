#pragma once

#include <QString>
#include <QJsonArray>

struct ParsedResponse {
    QString edits;
    QString answer;
    QString annotations;
    bool hasStructuredFormat = false;
};

class ResponseProcessor {
public:
    ParsedResponse parseStructuredResponse(const QString &rawResponse);
    QJsonArray parseSearchReplaceBlocks(const QString &text);
    QJsonArray parseAnnotations(const QString &text);
    QString extractFirstCodeBlock(const QString &text);

private:
    static constexpr const char* EDITS_TAG = "<<<EDITS>>>";
    static constexpr const char* ANSWER_TAG = "<<<ANSWER>>>";
    static constexpr const char* ANNOTATIONS_TAG = "<<<ANNOTATIONS>>>";
};