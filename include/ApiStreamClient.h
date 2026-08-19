#pragma once

#include "StreamEventParser.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QJsonArray>
#include <memory>

class ApiStreamClient : public QObject {
    Q_OBJECT

public:
    explicit ApiStreamClient(QNetworkAccessManager *networkManager, QObject *parent = nullptr);

    struct RequestParams {
        QString url;
        QJsonArray messages;
        QString model;
        double temperature;
        int timeoutMs;
    };

    void sendRequest(const RequestParams &params);
    void abortRequest();
    bool isActive() const { return m_currentReply != nullptr; }

    signals:
        void eventReceived(const StreamEvent &event);
    void requestFinished();
    void requestError(const QString &errorMessage);

private slots:
    void onReadyRead();
    void onFinished();
    void onError(QNetworkReply::NetworkError error);

private:
    QNetworkAccessManager *m_networkManager;
    QNetworkReply *m_currentReply = nullptr;
    QByteArray m_streamBuffer;
    std::unique_ptr<StreamEventParser> m_parser;
    bool m_requestFailed = false;
};