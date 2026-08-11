#include "../../include/window/MainWindow.h"
#include "../../include/ChatWidget.h"
#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QEvent>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);

    resize(800, 600);
    m_holeRect = QRect(200, 150, 400, 300);

    m_chatWidget = new ChatWidget(this);
    m_chatWidget->show();

    updateChatGeometry();
    updateClickThroughMask();
}

void MainWindow::updateChatGeometry() {
    if (!m_chatWidget) return;

    int chatX = 10;
    int chatY = 45;
    int chatWidth = m_holeRect.left() - 20;
    int chatHeight = height() - chatY - 10;

    if (chatWidth < 150) {
        chatWidth = 150;
    }

    m_chatWidget->setGeometry(chatX, chatY, chatWidth, chatHeight);
}

void MainWindow::updateClickThroughMask() {
    int w = width();
    int h = height();

    QRegion fullRegion(0, 0, w, h);

    int margin = m_handleSize / 2;
    QRect passthroughRect = m_holeRect.adjusted(margin, margin, -margin, -margin);

    QRegion holeRegion(passthroughRect);
    QRegion interactiveMask = fullRegion.subtracted(holeRegion);

    setMask(interactiveMask);
}

MainWindow::Handle MainWindow::handleAt(const QPoint &pos) const {
    if (!isMaximized()) {
        int bw = m_borderResizeWidth;
        int w = width();
        int h = height();

        bool top = pos.y() <= bw;
        bool bottom = pos.y() >= h - bw;
        bool left = pos.x() <= bw;
        bool right = pos.x() >= w - bw;

        if (top && left) return WinTopLeft;
        if (top && right) return WinTopRight;
        if (bottom && left) return WinBottomLeft;
        if (bottom && right) return WinBottomRight;
        if (top) return WinTop;
        if (bottom) return WinBottom;
        if (left) return WinLeft;
        if (right) return WinRight;
    }

    int hs = m_handleSize;

    QRect tl(m_holeRect.left() - hs/2, m_holeRect.top() - hs/2, hs, hs);
    QRect tr(m_holeRect.right() - hs/2, m_holeRect.top() - hs/2, hs, hs);
    QRect bl(m_holeRect.left() - hs/2, m_holeRect.bottom() - hs/2, hs, hs);
    QRect br(m_holeRect.right() - hs/2, m_holeRect.bottom() - hs/2, hs, hs);

    if (tl.contains(pos)) return HoleTopLeft;
    if (tr.contains(pos)) return HoleTopRight;
    if (bl.contains(pos)) return HoleBottomLeft;
    if (br.contains(pos)) return HoleBottomRight;

    QRect l(m_holeRect.left() - hs/2, m_holeRect.top() + hs/2, hs, m_holeRect.height() - hs);
    QRect r(m_holeRect.right() - hs/2, m_holeRect.top() + hs/2, hs, m_holeRect.height() - hs);
    QRect t(m_holeRect.left() + hs/2, m_holeRect.top() - hs/2, m_holeRect.width() - hs, hs);
    QRect b(m_holeRect.left() + hs/2, m_holeRect.bottom() - hs/2, m_holeRect.width() - hs, hs);

    if (l.contains(pos)) return HoleLeft;
    if (r.contains(pos)) return HoleRight;
    if (t.contains(pos)) return HoleTop;
    if (b.contains(pos)) return HoleBottom;

    return None;
}

void MainWindow::updateCursorShape(const QPoint &pos) {
    Handle h = handleAt(pos);
    switch (h) {
        case HoleTopLeft:
        case HoleBottomRight:
        case WinTopLeft:
        case WinBottomRight:
            setCursor(Qt::SizeFDiagCursor); break;
        case HoleTopRight:
        case HoleBottomLeft:
        case WinTopRight:
        case WinBottomLeft:
            setCursor(Qt::SizeBDiagCursor); break;
        case HoleLeft:
        case HoleRight:
        case WinLeft:
        case WinRight:
            setCursor(Qt::SizeHorCursor); break;
        case HoleTop:
        case HoleBottom:
        case WinTop:
        case WinBottom:
            setCursor(Qt::SizeVerCursor); break;
        default:
            setCursor(Qt::ArrowCursor); break;
    }
}

void MainWindow::toggleMaximize() {
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (m_closeButtonRect.contains(event->pos())) {
            close();
            return;
        }
        if (m_maxButtonRect.contains(event->pos())) {
            toggleMaximize();
            return;
        }
        if (m_minButtonRect.contains(event->pos())) {
            showMinimized();
            return;
        }

        m_activeHandle = handleAt(event->pos());
        if (m_activeHandle != None) {
            m_dragStartPos = event->globalPosition().toPoint();
            m_dragStartHoleRect = m_holeRect;
            m_dragStartWinGeometry = geometry();
        } else if (m_titleBarRect.contains(event->pos()) && !isMaximized()) {
            m_isDraggingWindow = true;
            m_windowDragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
    }
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_titleBarRect.contains(event->pos())) {
        if (!m_closeButtonRect.contains(event->pos()) &&
            !m_maxButtonRect.contains(event->pos()) &&
            !m_minButtonRect.contains(event->pos())) {
            toggleMaximize();
        }
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_isDraggingWindow) {
        move(event->globalPosition().toPoint() - m_windowDragStartPos);
        return;
    }

    if (m_activeHandle == None) {
        updateCursorShape(event->pos());
        return;
    }

    QPoint globalDelta = event->globalPosition().toPoint() - m_dragStartPos;

    if (m_activeHandle >= WinTop && m_activeHandle <= WinBottomRight) {
        QRect newWin = m_dragStartWinGeometry;

        switch (m_activeHandle) {
            case WinTopLeft: newWin.setTopLeft(m_dragStartWinGeometry.topLeft() + globalDelta); break;
            case WinTopRight: newWin.setTopRight(m_dragStartWinGeometry.topRight() + globalDelta); break;
            case WinBottomLeft: newWin.setBottomLeft(m_dragStartWinGeometry.bottomLeft() + globalDelta); break;
            case WinBottomRight: newWin.setBottomRight(m_dragStartWinGeometry.bottomRight() + globalDelta); break;
            case WinTop: newWin.setTop(m_dragStartWinGeometry.top() + globalDelta.y()); break;
            case WinBottom: newWin.setBottom(m_dragStartWinGeometry.bottom() + globalDelta.y()); break;
            case WinLeft: newWin.setLeft(m_dragStartWinGeometry.left() + globalDelta.x()); break;
            case WinRight: newWin.setRight(m_dragStartWinGeometry.right() + globalDelta.x()); break;
            default: break;
        }

        if (newWin.width() >= 200 && newWin.height() >= 150) {
            setGeometry(newWin);
        }
        return;
    }

    QRect newHole = m_dragStartHoleRect;

    switch (m_activeHandle) {
        case HoleTopLeft: newHole.setTopLeft(m_dragStartHoleRect.topLeft() + globalDelta); break;
        case HoleTopRight: newHole.setTopRight(m_dragStartHoleRect.topRight() + globalDelta); break;
        case HoleBottomLeft: newHole.setBottomLeft(m_dragStartHoleRect.bottomLeft() + globalDelta); break;
        case HoleBottomRight: newHole.setBottomRight(m_dragStartHoleRect.bottomRight() + globalDelta); break;
        case HoleLeft: newHole.setLeft(m_dragStartHoleRect.left() + globalDelta.x()); break;
        case HoleRight: newHole.setRight(m_dragStartHoleRect.right() + globalDelta.x()); break;
        case HoleTop: newHole.setTop(m_dragStartHoleRect.top() + globalDelta.y()); break;
        case HoleBottom: newHole.setBottom(m_dragStartHoleRect.bottom() + globalDelta.y()); break;
        default: break;
    }

    int minSize = 50;
    if (newHole.width() >= minSize && newHole.height() >= minSize &&
        newHole.left() >= 0 && newHole.top() >= 35 &&
        newHole.right() <= width() && newHole.bottom() <= height()) {
        m_holeRect = newHole;
        updateChatGeometry();
        updateClickThroughMask();
        update();
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_activeHandle = None;
        m_isDraggingWindow = false;
        updateCursorShape(event->pos());
    }
}

void MainWindow::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateChatGeometry();
        updateClickThroughMask();
        update();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateChatGeometry();
    updateClickThroughMask();
}

void MainWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    int w = width();
    int h = height();

    m_titleBarRect = QRect(0, 0, w, 35);
    painter.fillRect(m_titleBarRect, QColor(30, 30, 30, 240));

    painter.setPen(Qt::white);
    painter.drawText(QRect(15, 0, 200, 35), Qt::AlignVCenter | Qt::AlignLeft, "Talos Overlay");

    m_closeButtonRect = QRect(w - 35, 5, 30, 25);
    m_maxButtonRect   = QRect(w - 70, 5, 30, 25);
    m_minButtonRect   = QRect(w - 105, 5, 30, 25);

    painter.fillRect(m_closeButtonRect, QColor(200, 40, 40));
    painter.drawText(m_closeButtonRect, Qt::AlignCenter, "X");

    painter.fillRect(m_maxButtonRect, QColor(60, 60, 60));
    painter.drawText(m_maxButtonRect, Qt::AlignCenter, isMaximized() ? "❐" : "□");

    painter.fillRect(m_minButtonRect, QColor(60, 60, 60));
    painter.drawText(m_minButtonRect, Qt::AlignCenter, "_");

    QColor overlayColor(40, 40, 40, 180);

    int topBound = std::max(35, m_holeRect.top());

    painter.fillRect(0, 35, w, topBound - 35, overlayColor);
    painter.fillRect(0, m_holeRect.bottom(), w, h - m_holeRect.bottom(), overlayColor);
    painter.fillRect(0, topBound, m_holeRect.left(), m_holeRect.height(), overlayColor);
    painter.fillRect(m_holeRect.right(), topBound, w - m_holeRect.right(), m_holeRect.height(), overlayColor);

    painter.setPen(QPen(Qt::red, 2));
    painter.drawRect(m_holeRect);

    int hs = m_handleSize;
    painter.setBrush(Qt::white);
    painter.setPen(QPen(Qt::red, 1));

    painter.drawRect(m_holeRect.left() - hs/2, m_holeRect.top() - hs/2, hs, hs);
    painter.drawRect(m_holeRect.right() - hs/2, m_holeRect.top() - hs/2, hs, hs);
    painter.drawRect(m_holeRect.left() - hs/2, m_holeRect.bottom() - hs/2, hs, hs);
    painter.drawRect(m_holeRect.right() - hs/2, m_holeRect.bottom() - hs/2, hs, hs);
}