#include "../include/ApiStreamClient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>


ApiStreamClient::ApiStreamClient(
    QNetworkAccessManager *networkManager,
    QObject *parent
)
    : QObject(parent)
    , m_networkManager(networkManager)
    , m_parser(
          std::make_unique<
              StreamEventParser
          >()
      )
{
    m_parser->onEvent(
        [this](
            const StreamEvent &event
        ) {
            emit eventReceived(
                event
            );
        }
    );
}


void ApiStreamClient::sendRequest(
    const RequestParams &params
)
{
    abortRequest();

    if (!m_networkManager) {

        emit requestError(
            QStringLiteral(
                "No QNetworkAccessManager is available."
            )
        );

        return;
    }

    QUrl url(
        params.url
    );

    if (url.scheme().isEmpty()) {

        url.setScheme(
            QStringLiteral(
                "http"
            )
        );
    }

    if (!url.isValid()) {

        emit requestError(
            QStringLiteral(
                "Invalid URL: %1"
            ).arg(
                url.errorString()
            )
        );

        return;
    }

    if (params.model.trimmed().isEmpty()) {

        emit requestError(
            QStringLiteral(
                "No LLM model identifier was supplied."
            )
        );

        return;
    }

    QNetworkRequest request(
        url
    );

    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QStringLiteral(
            "application/json"
        )
    );

    request.setRawHeader(
        QByteArray(
            "Accept"
        ),
        QByteArray(
            "text/event-stream"
        )
    );

    request.setAttribute(
        QNetworkRequest::
            RedirectPolicyAttribute,
        QNetworkRequest::
            NoLessSafeRedirectPolicy
    );

    request.setTransferTimeout(
        params.timeoutMs
    );

    QJsonObject body;

    body.insert(
        QStringLiteral(
            "model"
        ),
        params.model
    );

    body.insert(
        QStringLiteral(
            "messages"
        ),
        params.messages
    );

    body.insert(
        QStringLiteral(
            "stream"
        ),
        true
    );

    body.insert(
        QStringLiteral(
            "temperature"
        ),
        params.temperature
    );

    const QByteArray payload =
        QJsonDocument(
            body
        ).toJson(
            QJsonDocument::Compact
        );

    qDebug()
        << "[ApiStreamClient] POST"
        << url;

    qDebug()
        << "[ApiStreamClient] model="
        << params.model;

    qDebug()
        << "[ApiStreamClient] messages="
        << params.messages.size();

    m_streamBuffer.clear();

    m_requestFailed =
        false;

    m_currentReply =
        m_networkManager->post(
            request,
            payload
        );

    connect(
        m_currentReply,
        &QNetworkReply::readyRead,
        this,
        &ApiStreamClient::onReadyRead
    );

    connect(
        m_currentReply,
        &QNetworkReply::finished,
        this,
        &ApiStreamClient::onFinished
    );

    connect(
        m_currentReply,
        QOverload<
            QNetworkReply::NetworkError
        >::of(
            &QNetworkReply::errorOccurred
        ),
        this,
        &ApiStreamClient::onError
    );
}


void ApiStreamClient::abortRequest()
{
    if (!m_currentReply) {

        m_streamBuffer.clear();
        m_requestFailed =
            false;

        return;
    }

    QNetworkReply *reply =
        m_currentReply;

    m_currentReply =
        nullptr;

    reply->abort();
    reply->deleteLater();

    m_streamBuffer.clear();
    m_requestFailed =
        false;
}


void ApiStreamClient::processBufferedLines(
    bool flushPartialLine
)
{
    while (
        m_streamBuffer.contains(
            '\n'
        )
    ) {

        const int idx =
            m_streamBuffer.indexOf(
                '\n'
            );

        QByteArray line =
            m_streamBuffer
                .left(
                    idx
                )
                .trimmed();

        m_streamBuffer.remove(
            0,
            idx + 1
        );

        /*
         * Empty SSE lines delimit events and don't
         * need to be passed to the parser.
         */
        if (!line.isEmpty()) {

            m_parser->processLine(
                line
            );
        }
    }

    /*
     * A server can finish with a final event that does
     * not have a trailing newline. Don't silently lose it.
     */
    if (
        flushPartialLine &&
        !m_streamBuffer.trimmed().isEmpty()
    ) {

        const QByteArray line =
            m_streamBuffer.trimmed();

        m_streamBuffer.clear();

        m_parser->processLine(
            line
        );
    }
}


void ApiStreamClient::onReadyRead()
{
    if (
        !m_currentReply ||
        m_requestFailed
    ) {
        return;
    }

    m_streamBuffer.append(
        m_currentReply
            ->readAll()
    );

    processBufferedLines(
        false
    );
}


void ApiStreamClient::onFinished()
{
    if (!m_currentReply)
        return;

    QNetworkReply *reply =
        m_currentReply;

    m_currentReply =
        nullptr;

    /*
     * Read any bytes that arrived after the final
     * readyRead signal.
     */
    m_streamBuffer.append(
        reply->readAll()
    );

    processBufferedLines(
        true
    );

    const int httpStatus =
        reply->attribute(
            QNetworkRequest::
                HttpStatusCodeAttribute
        ).toInt();

    const bool failed =
        m_requestFailed ||
        reply->error() !=
            QNetworkReply::NoError;

    const QString errorString =
        reply->errorString();

    reply->deleteLater();

    if (failed) {

        /*
         * onError() normally emits the error already.
         * Keep finished() from emitting a duplicate.
         */
        return;
    }

    if (
        httpStatus >= 400 &&
        httpStatus != 0
    ) {

        emit requestError(
            QStringLiteral(
                "HTTP error %1"
            ).arg(
                httpStatus
            )
        );

        return;
    }

    emit requestFinished();
}


void ApiStreamClient::onError(
    QNetworkReply::NetworkError error
)
{
    if (m_requestFailed)
        return;

    m_requestFailed =
        true;

    QString message =
        QStringLiteral(
            "Network error: %1"
        ).arg(
            static_cast<int>(
                error
            )
        );

    if (m_currentReply) {

        const int httpStatus =
            m_currentReply
                ->attribute(
                    QNetworkRequest::
                        HttpStatusCodeAttribute
                )
                .toInt();

        if (httpStatus > 0) {

            message +=
                QStringLiteral(
                    " HTTP %1"
                ).arg(
                    httpStatus
                );
        }

        const QByteArray body =
            m_currentReply
                ->readAll();

        if (!body.isEmpty()) {

            qWarning()
                << "[ApiStreamClient] HTTP response body:"
                << body;

            message +=
                QStringLiteral(
                    ": %1"
                ).arg(
                    QString::fromUtf8(
                        body
                    ).trimmed()
                );
        }

        const QVariant redirect =
            m_currentReply->attribute(
                QNetworkRequest::
                    RedirectionTargetAttribute
            );

        if (redirect.isValid()) {

            const QUrl redirectedUrl =
                m_currentReply
                    ->url()
                    .resolved(
                        redirect.toUrl()
                    );

            message +=
                QStringLiteral(
                    " (redirect to %1)"
                ).arg(
                    redirectedUrl.toString()
                );
        }

        if (
            !m_currentReply
                ->errorString()
                .isEmpty()
        ) {

            message +=
                QStringLiteral(
                    ": %1"
                ).arg(
                    m_currentReply
                        ->errorString()
                );
        }

        qWarning()
            << "[ApiStreamClient]"
            << message;
    }

    emit requestError(
        message
    );
}