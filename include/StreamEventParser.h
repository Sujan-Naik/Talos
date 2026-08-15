#pragma once

#include <QString>
#include <QJsonObject>
#include <QJsonArray>
#include <functional>

enum class StreamEventType {
    TextDelta,
    ToolCall,
    ToolResult,
    Done,
    Error
};

struct StreamEvent {
    StreamEventType type;
    QString data;
    QJsonObject metadata;
};

class StreamEventParser {
public:
    using EventCallback = std::function<void(const StreamEvent&)>;

    void onEvent(const EventCallback &callback) { m_eventCallback = callback; }
    void processLine(const QByteArray &line);

private:
    EventCallback m_eventCallback;
    void emitEvent(const StreamEvent &event);
    StreamEvent parseOpenAiFormat(const QJsonObject &root);
};