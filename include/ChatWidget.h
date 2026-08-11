#ifndef TALOS_CHATWIDGET_H
#define TALOS_CHATWIDGET_H

#include <QWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QJsonArray>
#include "CaptureOverlay.h"

enum ChatItemRoles {
    SenderRole = Qt::UserRole + 1,
    TextRole,
    IsUserRole
};

class ChatDelegate : public QStyledItemDelegate {
Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

class ChatWidget : public QWidget {
Q_OBJECT
public:
    explicit ChatWidget(QWidget *parent = nullptr);
    void appendMessage(const QString &text, bool isUser);

signals:
    void messageSent(const QString &text);

private slots:
    void sendApiRequest();
    void handleReadyRead();
    void handleReplyFinished();
    void captureAndSetText();

private:
    void appendToCurrentAiMessage(const QString &deltaText);

    QListWidget *m_listWidget;
    QLineEdit *m_inputBox;
    QPushButton *m_sendButton;
    QPushButton *m_captureButton;
    CaptureOverlay *m_overlay = nullptr; // <--- Overlay instance
    QNetworkAccessManager *m_networkManager;
    QPointer<QNetworkReply> m_currentReply;
    QListWidgetItem *m_currentAiItem = nullptr;
    QByteArray m_streamBuffer;
    QJsonArray m_conversationHistory;
};

#endif // TALOS_CHATWIDGET_H