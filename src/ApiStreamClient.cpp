#include "../include/ApiStreamClient.h"

#include <QNetworkRequest>
#include <QJsonDocument>
#include <QUrl>
#include <QDebug>

ApiStreamClient::ApiStreamClient(
    QNetworkAccessManager *networkManager,
    QObject *parent
)
    : QObject(parent)
    , m_networkManager(networkManager)
    , m_parser(std::make_unique<StreamEventParser>())
{
    m_parser->onEvent([this](const StreamEvent &event) {
        emit eventReceived(event);
    });
}

void ApiStreamClient::sendRequest(
    const RequestParams &params
)
{
    abortRequest();

    QUrl url(params.url);

    if (url.scheme().isEmpty()) {
        url.setScheme(QStringLiteral("http"));
    }

    if (!url.isValid()) {
        emit requestError(
            QStringLiteral(
                "Invalid URL: %1"
            ).arg(url.errorString())
        );
        return;
    }

    QNetworkRequest request(url);

    request.setHeader(
        QNetworkRequest::ContentTypeHeader,
        QStringLiteral("application/json")
    );

    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::NoLessSafeRedirectPolicy
    );

    request.setTransferTimeout(
        params.timeoutMs
    );

    QJsonObject body;

    body.insert(
        QStringLiteral("model"),
        params.model
    );

    body.insert(
        QStringLiteral("messages"),
        params.messages
    );

    body.insert(
        QStringLiteral("stream"),
        true
    );

    body.insert(
        QStringLiteral("temperature"),
        params.temperature
    );

    m_streamBuffer.clear();
    m_requestFailed = false;

    m_currentReply =
        m_networkManager->post(
            request,
            QJsonDocument(body).toJson()
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
        QOverload<QNetworkReply::NetworkError>::of(
            &QNetworkReply::errorOccurred
        ),
        this,
        &ApiStreamClient::onError
    );
}

void ApiStreamClient::abortRequest()
{
    if (!m_currentReply) {
        return;
    }

    m_currentReply->abort();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;
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
        m_currentReply->readAll()
    );

    while (
        m_streamBuffer.contains('\n')
    ) {
        const int idx =
            m_streamBuffer.indexOf('\n');

        QByteArray line =
            m_streamBuffer
                .left(idx)
                .trimmed();

        m_streamBuffer.remove(
            0,
            idx + 1
        );

        if (!line.isEmpty()) {
            m_parser->processLine(line);
        }
    }
}

void ApiStreamClient::onFinished()
{
    if (!m_currentReply) {
        return;
    }

    QNetworkReply *reply =
        m_currentReply;

    m_currentReply = nullptr;

    const bool failed =
        m_requestFailed ||
        reply->error() != QNetworkReply::NoError;

    reply->deleteLater();

    if (failed) {
        return;
    }

    emit requestFinished();
}

void ApiStreamClient::onError(
    QNetworkReply::NetworkError error
)
{
    m_requestFailed = true;

    QString message =
        QStringLiteral(
            "Network error: %1"
        ).arg(
            static_cast<int>(error)
        );

    if (m_currentReply) {
        const QVariant redirect =
            m_currentReply->attribute(
                QNetworkRequest::RedirectionTargetAttribute
            );

        if (redirect.isValid()) {
            const QUrl redirectedUrl =
                m_currentReply
                    ->url()
                    .resolved(
                        redirect.toUrl()
                    );

            message += QStringLiteral(
                " (redirect to %1)"
            ).arg(
                redirectedUrl.toString()
            );
        }

        if (!m_currentReply->errorString().isEmpty()) {
            message += QStringLiteral(
                ": %1"
            ).arg(
                m_currentReply->errorString()
            );
        }
    }

    emit requestError(message);
}