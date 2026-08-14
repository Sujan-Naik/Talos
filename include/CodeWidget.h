#ifndef CODEWIDGET_H
#define CODEWIDGET_H

#include <QWidget>
#include <QJsonArray>
#include <QNetworkReply>

class QTextEdit;
class QPushButton;
class QLabel;
class QWebEngineView;
class QNetworkAccessManager;
class CodeBackend;

class CodeWidget : public QWidget
{
    Q_OBJECT
public:
    explicit CodeWidget(QWidget *parent = nullptr);
    ~CodeWidget() override = default;

    void setEditorCode(const QString &code, const QString &language = QString());
    void appendCodeToEditor(const QString &code);
    QString editorCode() const;

    signals:
        void messageSent(const QString &text);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void handleReadyRead();
    void handleReplyFinished();

private:
    void sendApiRequest();
    void appendMessageAsUser(const QString &text);
    void appendMessageAsAi(const QString &text);
    void showWarningBubble(const QString &message);

    QWebEngineView *m_webEngineView = nullptr;
    QTextEdit *m_inputBox = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QLabel *m_statusLabel = nullptr;
    QNetworkAccessManager *m_networkManager = nullptr;
    CodeBackend *m_backend = nullptr;

    QNetworkReply *m_currentReply = nullptr;
    QByteArray m_streamBuffer;
    QString m_aiStreamAccumulator;
    bool m_isStreamingAi = false;
    bool m_isPageLoaded = false;
    QString m_currentCode;
    QJsonArray m_conversationHistory;
};

#endif // CODEWIDGET_H