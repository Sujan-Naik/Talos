#ifndef TALOS_MAINWINDOW_H
#define TALOS_MAINWINDOW_H

#include <QWidget>
#include <QRegion>
#include <QRect>
#include <QPoint>

class ChatWidget;

class MainWindow : public QWidget {
Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;
    QRect holeRect() const { return m_holeRect; }
protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    enum Handle {
        None,
        HoleTopLeft, HoleTopRight, HoleBottomLeft, HoleBottomRight, HoleLeft, HoleRight, HoleTop, HoleBottom,
        WinTop, WinBottom, WinLeft, WinRight, WinTopLeft, WinTopRight, WinBottomLeft, WinBottomRight
    };

    void updateClickThroughMask();
    Handle handleAt(const QPoint &pos) const;
    void updateCursorShape(const QPoint &pos);
    void toggleMaximize();
    void updateChatGeometry();

    QRect m_holeRect;
    Handle m_activeHandle = None;
    QPoint m_dragStartPos;
    QRect m_dragStartHoleRect;
    QRect m_dragStartWinGeometry;
    int m_handleSize = 10;
    int m_borderResizeWidth = 8;

    QRect m_closeButtonRect;
    QRect m_maxButtonRect;
    QRect m_minButtonRect;
    QRect m_titleBarRect;
    bool m_isDraggingWindow = false;
    QPoint m_windowDragStartPos;

    ChatWidget *m_chatWidget = nullptr;
};

#endif // TALOS_MAINWINDOW_H