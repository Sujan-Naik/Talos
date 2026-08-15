#include "StreamEventParser.h"
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDebug>

void StreamEventParser::processLine(const QByteArray &line) {
    if (line.isEmpty() || line == "[DONE]") return;

    QByteArray data = line;
    if (data.startsWith("data: ")) data = data.mid(6).trimmed();

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) return;

    StreamEvent event = parseOpenAiFormat(doc.object());
    emitEvent(event);
}

StreamEvent StreamEventParser::parseOpenAiFormat(const QJsonObject &root) {
    StreamEvent event;
    event.type = StreamEventType::TextDelta;
    event.metadata = root;

    if (root.contains("choices")) {
        const QJsonArray choices = root.value("choices").toArray();
        if (!choices.isEmpty()) {
            const QJsonObject choice = choices.at(0).toObject();
            if (choice.contains("delta")) {
                const QJsonObject delta = choice.value("delta").toObject();
                if (delta.contains("content")) {
                    event.data = delta.value("content").toString();
                }
            } else if (choice.contains("text")) {
                event.data = choice.value("text").toString();
            }
        }
    } else if (root.contains("response")) {
        event.data = root.value("response").toString();
    } else if (root.contains("message")) {
        const QJsonObject msg = root.value("message").toObject();
        if (msg.contains("content")) {
            event.data = msg.value("content").toString();
        }
    }

    return event;
}

void StreamEventParser::emitEvent(const StreamEvent &event) {
    if (m_eventCallback) {
        m_eventCallback(event);
    }
}
