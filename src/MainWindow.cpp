#include "../include/MainWindow.h"
#include "../include/ChatWidget.h"
#include "../include/ChatBackend.h"
#include "../include/AudioRecorder.h"
#include "../include/WakeWordDetector.h"
#include "../include/WhisperTranscriber.h"

#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QEvent>
#include <QDir>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>
#include <QDebug>
#include <QInputDialog>
#include <QWebEngineView>
#include <algorithm>

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);

    resize(800, 600);
    m_savedNormalGeometry = QRect(100, 100, 800, 600);
    m_holeRect = QRect(200, 150, 400, 300);
    m_previousHoleRect = m_holeRect;

    m_chatWidget = new ChatWidget(this);
    m_chatWidget->setHoleRect(m_holeRect, m_holeEnabled);

    installWebEventFilters();

    updateChatGeometry();
    updateClickThroughMask();

    m_audioRecorder = new AudioRecorder(this);

    QString modelsDir = QDir(QCoreApplication::applicationDirPath()).filePath("models");

    m_wakeWordDetector = new WakeWordDetector(
            QDir(modelsDir).filePath("melspectrogram.onnx"),
            QDir(modelsDir).filePath("embedding_model.onnx"),
            QDir(modelsDir).filePath("hey_jarvis.onnx"),
            0.5f,
            this
    );
    m_transcriber = new WhisperTranscriber("ggml-tiny.en.bin", this);

    connect(m_audioRecorder, &AudioRecorder::audioChunkReady,
            m_wakeWordDetector, &WakeWordDetector::processAudioChunk);

    connect(m_wakeWordDetector, &WakeWordDetector::wakeWordDetected, this, [this](float confidence) {
        qDebug() << "[Talos] Wake Word Detected! Confidence:" << confidence;

        QTimer::singleShot(0, this, [this]() {
            m_appState = StateTriggered;
            update();

            m_audioRecorder->startBufferingSpeech();

            QTimer::singleShot(5000, this, &MainWindow::onSpeechCaptureFinished);
        });
    });

    const auto devices = m_audioRecorder->availableInputDevices();
    if (!devices.isEmpty()) {
        m_audioRecorder->setInputDevice(QMediaDevices::defaultAudioInput());
    }
}

void MainWindow::installWebEventFilters() {
    if (!m_chatWidget) return;

    m_chatWidget->installEventFilter(this);
    if (auto *view = m_chatWidget->webView()) {
        view->installEventFilter(this);
        if (view->focusProxy()) {
            view->focusProxy()->installEventFilter(this);
        }
    }
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ChildAdded) {
        // Intercept dynamically spawned child render proxies of QWebEngineView
        auto *ce = static_cast<QChildEvent*>(event);
        if (ce->child()) {
            ce->child()->installEventFilter(this);
        }
    }

    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseMove ||
        event->type() == QEvent::MouseButtonRelease) {

        auto *me = static_cast<QMouseEvent*>(event);
        QPoint globalPos = me->globalPosition().toPoint();
        QPoint localPos = mapFromGlobal(globalPos);

        Handle h = handleAt(localPos);

        if (event->type() == QEvent::MouseMove) {
            if (m_activeHandle != Handle::None) {
                // Synthesize mouse move to MainWindow handler
                QMouseEvent syntheticEvent(QEvent::MouseMove, QPointF(localPos), QPointF(globalPos),
                                           me->button(), me->buttons(), me->modifiers());
                mouseMoveEvent(&syntheticEvent);
                return true; // Intercept & consume event
            } else {
                updateCursorShape(localPos);
                if (h != Handle::None) {
                    return false;
                }
            }
        } else if (event->type() == QEvent::MouseButtonPress) {
            if (me->button() == Qt::LeftButton && h != Handle::None) {
                QMouseEvent syntheticEvent(QEvent::MouseButtonPress, QPointF(localPos), QPointF(globalPos),
                                           me->button(), me->buttons(), me->modifiers());
                mousePressEvent(&syntheticEvent);
                return true; // Swallow click from web engine
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (me->button() == Qt::LeftButton && m_activeHandle != Handle::None) {
                QMouseEvent syntheticEvent(QEvent::MouseButtonRelease, QPointF(localPos), QPointF(globalPos),
                                           me->button(), me->buttons(), me->modifiers());
                mouseReleaseEvent(&syntheticEvent);
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void MainWindow::chooseAudioDevice() {
    const auto devices = m_audioRecorder->availableInputDevices();
    if (devices.isEmpty()) return;

    QStringList names;
    int currentIndex = 0;
    QAudioDevice current = m_audioRecorder->currentDevice();

    for (int i = 0; i < devices.size(); ++i) {
        names << devices[i].description();
        if (!current.isNull() && devices[i].id() == current.id())
            currentIndex = i;
    }

    bool ok = false;
    QString chosen = QInputDialog::getItem(this, "Select Microphone", "Audio input device:", names, currentIndex, false, &ok);

    if (ok && !chosen.isEmpty()) {
        for (const QAudioDevice &dev : devices) {
            if (dev.description() == chosen) {
                m_audioRecorder->setInputDevice(dev);
                break;
            }
        }
    }
}

void MainWindow::toggleHole(bool enabled) {
    m_holeEnabled = enabled;
    updateChatGeometry();
    updateClickThroughMask();

    if (!m_holeEnabled) {
        clearMask();
    }

    if (m_chatWidget) {
        m_chatWidget->setHoleRect(m_holeRect, m_holeEnabled);
        m_chatWidget->update();
    }

    m_chatWidget->setHoleEnabled(m_holeEnabled);
    update();

}

void MainWindow::resetHole() {
    int w = width();
    int h = height();
    int titleBarHeight = 35;
    m_previousHoleRect = m_holeRect;
    m_holeRect = QRect((w - 400) / 2, std::max(titleBarHeight + 10, (h - 300) / 2), 400, 300);
    m_holeEnabled = true;

    updateChatGeometry();
    updateClickThroughMask();

    if (m_chatWidget) {
        m_chatWidget->setHoleEnabled(true);  
        m_chatWidget->setHoleRect(m_holeRect, m_holeEnabled);
        m_chatWidget->update();
    }

    update();
}

void MainWindow::onSpeechCaptureFinished() {
    m_appState = StateProcessing;
    update();

    std::vector<float> pcmData = m_audioRecorder->stopBufferingSpeech();

    if (pcmData.empty()) {
        m_wakeWordDetector->reset();
        m_appState = StateListening;
        update();
        m_audioRecorder->startListening();
        return;
    }

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        QString transcribedText = watcher->result();
        watcher->deleteLater();

        if (!transcribedText.trimmed().isEmpty() && m_chatWidget && m_chatWidget->backend()) {
            emit m_chatWidget->backend()->appendUserMessage(transcribedText);
            m_chatWidget->backend()->onUserSendMessage(transcribedText);
        }

        m_wakeWordDetector->reset();
        m_appState = StateListening;
        update();
        m_audioRecorder->startListening();
    });

    QFuture<QString> future = QtConcurrent::run([pcmData, this]() {
        return m_transcriber->transcribe(pcmData);
    });
    watcher->setFuture(future);
}

void MainWindow::updateChatGeometry() {
    if (!m_chatWidget) return;

    int titleBarHeight = 35;
    m_chatWidget->setGeometry(0, titleBarHeight, width(), height() - titleBarHeight);
    m_chatWidget->setHoleRect(m_holeRect, m_holeEnabled);
}

void MainWindow::updateClickThroughMask() {
    int w = width();
    int h = height();

    if (m_holeEnabled && m_holeRect.isValid()) {
        QRegion fullRegion(0, 0, w, h);
        QRect passthroughRect = m_holeRect.adjusted(m_bufferSize, m_bufferSize, -m_bufferSize, -m_bufferSize);

        if (passthroughRect.isValid() && passthroughRect.width() > 0 && passthroughRect.height() > 0) {
            QRegion holeRegion(passthroughRect);
            QRegion interactiveMask = fullRegion.subtracted(holeRegion);

            clearMask();
            setMask(interactiveMask);
        } else {
            clearMask();
        }
    } else {
        clearMask();
    }
}

MainWindow::Handle MainWindow::handleAt(const QPoint &pos) const {
    if (!isMaximized() && !isFullScreen()) {
        int bw = m_borderResizeWidth;
        int w = width();
        int h = height();

        bool top = pos.y() <= bw;
        bool bottom = pos.y() >= h - bw;
        bool left = pos.x() <= bw;
        bool right = pos.x() >= w - bw;

        if (top && left) return Handle::WinTopLeft;
        if (top && right) return Handle::WinTopRight;
        if (bottom && left) return Handle::WinBottomLeft;
        if (bottom && right) return Handle::WinBottomRight;
        if (top) return Handle::WinTop;
        if (bottom) return Handle::WinBottom;
        if (left) return Handle::WinLeft;
        if (right) return Handle::WinRight;
    }

    if (!m_holeEnabled) return Handle::None;

    int hs = m_handleSize;
    QRect hole = m_holeRect;

    // Generous hitbox handles around hole edges
    QRect tl(hole.left() - hs / 2, hole.top() - hs / 2, hs * 2, hs * 2);
    QRect tr(hole.right() - hs / 2, hole.top() - hs / 2, hs * 2, hs * 2);
    QRect bl(hole.left() - hs / 2, hole.bottom() - hs / 2, hs * 2, hs * 2);
    QRect br(hole.right() - hs / 2, hole.bottom() - hs / 2, hs * 2, hs * 2);

    if (tl.contains(pos)) return Handle::HoleTopLeft;
    if (tr.contains(pos)) return Handle::HoleTopRight;
    if (bl.contains(pos)) return Handle::HoleBottomLeft;
    if (br.contains(pos)) return Handle::HoleBottomRight;

    QRect l(hole.left() - hs / 2, hole.top() + hs, hs * 2, hole.height() - 2 * hs);
    QRect r(hole.right() - hs / 2, hole.top() + hs, hs * 2, hole.height() - 2 * hs);
    QRect t(hole.left() + hs, hole.top() - hs / 2, hole.width() - 2 * hs, hs * 2);
    QRect b(hole.left() + hs, hole.bottom() - hs / 2, hole.width() - 2 * hs, hs * 2);

    if (l.contains(pos)) return Handle::HoleLeft;
    if (r.contains(pos)) return Handle::HoleRight;
    if (t.contains(pos)) return Handle::HoleTop;
    if (b.contains(pos)) return Handle::HoleBottom;

    return Handle::None;
}

void MainWindow::updateCursorShape(const QPoint &pos) {
    Handle h = handleAt(pos);
    switch (h) {
        case Handle::HoleTopLeft:
        case Handle::HoleBottomRight:
        case Handle::WinTopLeft:
        case Handle::WinBottomRight:
            setCursor(Qt::SizeFDiagCursor); break;
        case Handle::HoleTopRight:
        case Handle::HoleBottomLeft:
        case Handle::WinTopRight:
        case Handle::WinBottomLeft:
            setCursor(Qt::SizeBDiagCursor); break;
        case Handle::HoleLeft:
        case Handle::HoleRight:
        case Handle::WinLeft:
        case Handle::WinRight:
            setCursor(Qt::SizeHorCursor); break;
        case Handle::HoleTop:
        case Handle::HoleBottom:
        case Handle::WinTop:
        case Handle::WinBottom:
            setCursor(Qt::SizeVerCursor); break;
        default:
            setCursor(Qt::ArrowCursor); break;
    }
}

void MainWindow::toggleMaximize() {
    if (isMaximized() || isFullScreen()) {
        showNormal();
        if (m_savedNormalGeometry.isValid()) {
            setGeometry(m_savedNormalGeometry);
        }
    } else {
        m_savedNormalGeometry = geometry();
        showMaximized();
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton && m_titleBarRect.contains(event->pos())) {
        chooseAudioDevice();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        if (m_closeButtonRect.contains(event->pos())) { close(); return; }
        if (m_maxButtonRect.contains(event->pos())) { toggleMaximize(); return; }
        if (m_minButtonRect.contains(event->pos())) { showMinimized(); return; }
        if (m_toggleHoleBtnRect.contains(event->pos())) { toggleHole(!m_holeEnabled); return; }
        if (m_resetHoleBtnRect.contains(event->pos())) { resetHole(); return; }

        m_activeHandle = handleAt(event->pos());
        if (m_activeHandle != Handle::None) {
            m_dragStartPos = event->globalPosition().toPoint();
            m_dragStartHoleRect = m_holeRect;
            m_dragStartWinGeometry = geometry();
        } else if (m_titleBarRect.contains(event->pos())) {
            m_isDraggingWindow = true;
            m_windowDragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
        }
    }
}

void MainWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_titleBarRect.contains(event->pos())) {
        if (!m_closeButtonRect.contains(event->pos()) &&
            !m_maxButtonRect.contains(event->pos()) &&
            !m_minButtonRect.contains(event->pos()) &&
            !m_toggleHoleBtnRect.contains(event->pos()) &&
            !m_resetHoleBtnRect.contains(event->pos())) {
            toggleMaximize();
        }
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent *event) {
    if (m_isDraggingWindow) {
        if (isMaximized() || isFullScreen()) {
            QPoint globalPos = event->globalPosition().toPoint();
            int dragDistanceY = globalPos.y() - m_windowDragStartPos.y();

            if (dragDistanceY > 5 || globalPos.y() > 10) {
                qreal relativeX = static_cast<qreal>(globalPos.x()) / width();
                showNormal();

                int newWidth = m_savedNormalGeometry.isValid() ? m_savedNormalGeometry.width() : 800;
                int newHeight = m_savedNormalGeometry.isValid() ? m_savedNormalGeometry.height() : 600;
                int newX = globalPos.x() - static_cast<int>(newWidth * relativeX);
                int newY = globalPos.y() - 15;

                setGeometry(newX, newY, newWidth, newHeight);
                m_windowDragStartPos = QPoint(static_cast<int>(newWidth * relativeX), 15);
            }
            return;
        }

        move(event->globalPosition().toPoint() - m_windowDragStartPos);
        return;
    }

    if (m_activeHandle == Handle::None) {
        updateCursorShape(event->pos());
        return;
    }

    QPoint globalDelta = event->globalPosition().toPoint() - m_dragStartPos;

    if (m_activeHandle >= Handle::WinTop && m_activeHandle <= Handle::WinBottomRight) {
        QRect newWin = m_dragStartWinGeometry;

        switch (m_activeHandle) {
            case Handle::WinTopLeft: newWin.setTopLeft(m_dragStartWinGeometry.topLeft() + globalDelta); break;
            case Handle::WinTopRight: newWin.setTopRight(m_dragStartWinGeometry.topRight() + globalDelta); break;
            case Handle::WinBottomLeft: newWin.setBottomLeft(m_dragStartWinGeometry.bottomLeft() + globalDelta); break;
            case Handle::WinBottomRight: newWin.setBottomRight(m_dragStartWinGeometry.bottomRight() + globalDelta); break;
            case Handle::WinTop: newWin.setTop(m_dragStartWinGeometry.top() + globalDelta.y()); break;
            case Handle::WinBottom: newWin.setBottom(m_dragStartWinGeometry.bottom() + globalDelta.y()); break;
            case Handle::WinLeft: newWin.setLeft(m_dragStartWinGeometry.left() + globalDelta.x()); break;
            case Handle::WinRight: newWin.setRight(m_dragStartWinGeometry.right() + globalDelta.x()); break;
            default: break;
        }

        if (newWin.width() >= 200 && newWin.height() >= 150) {
            setGeometry(newWin);
            m_savedNormalGeometry = newWin;
        }
        return;
    }

    QRect newHole = m_dragStartHoleRect;

    switch (m_activeHandle) {
        case Handle::HoleTopLeft:
            newHole.setTopLeft(m_dragStartHoleRect.topLeft() + globalDelta);
            break;
        case Handle::HoleTopRight:
            newHole.setTopRight(m_dragStartHoleRect.topRight() + globalDelta);
            break;
        case Handle::HoleBottomLeft:
            newHole.setBottomLeft(m_dragStartHoleRect.bottomLeft() + globalDelta);
            break;
        case Handle::HoleBottomRight:
            newHole.setBottomRight(m_dragStartHoleRect.bottomRight() + globalDelta);
            break;
        case Handle::HoleLeft:
            newHole.setLeft(m_dragStartHoleRect.left() + globalDelta.x());
            break;
        case Handle::HoleRight:
            newHole.setRight(m_dragStartHoleRect.right() + globalDelta.x());
            break;
        case Handle::HoleTop:
            newHole.setTop(m_dragStartHoleRect.top() + globalDelta.y());
            break;
        case Handle::HoleBottom:
            newHole.setBottom(m_dragStartHoleRect.bottom() + globalDelta.y());
            break;
        default: break;
    }

    int minSize = 100;
    int titleBarHeight = 35;
    if (newHole.width() >= minSize && newHole.height() >= minSize &&
        newHole.left() >= 0 && newHole.top() >= titleBarHeight &&
        newHole.right() <= width() && newHole.bottom() <= height()) {
        m_previousHoleRect = m_holeRect;
        m_holeRect = newHole;
        updateChatGeometry();
        updateClickThroughMask();

        if (m_chatWidget) {
            m_chatWidget->setHoleRect(m_holeRect, m_holeEnabled);
        }
        update();
    }
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        m_activeHandle = Handle::None;
        m_isDraggingWindow = false;
        if (!isMaximized() && !isFullScreen()) {
            m_savedNormalGeometry = geometry();
        }
        updateCursorShape(event->pos());
    }
}

void MainWindow::changeEvent(QEvent *event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange) {
        updateChatGeometry();
        updateClickThroughMask();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    if (!isMaximized() && !isFullScreen() && !m_isDraggingWindow) {
        m_savedNormalGeometry = geometry();
    }
    updateChatGeometry();
    updateClickThroughMask();

    if (m_chatWidget) {
        m_chatWidget->setHoleRect(m_holeRect, m_holeEnabled);
        m_chatWidget->update();
    }
}

void MainWindow::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    int w = width();

    m_titleBarRect = QRect(0, 0, w, 35);
    painter.fillRect(m_titleBarRect, QColor(30, 30, 30, 240));

    QColor indicatorColor;
    QString statusText;
    if (m_appState == StateListening) {
        indicatorColor = QColor(46, 204, 113);
        statusText = "Listening...";
    } else if (m_appState == StateTriggered) {
        indicatorColor = QColor(241, 196, 15);
        statusText = "Recording speech...";
    } else {
        indicatorColor = QColor(52, 152, 219);
        statusText = "Transcribing...";
    }

    painter.setBrush(indicatorColor);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(20, 17.5), 5, 5);

    painter.setPen(Qt::white);
    painter.drawText(QRect(35, 0, 220, 35), Qt::AlignVCenter | Qt::AlignLeft, "Talos Overlay — " + statusText);

    m_closeButtonRect   = QRect(w - 35, 5, 30, 25);
    m_maxButtonRect     = QRect(w - 70, 5, 30, 25);
    m_minButtonRect     = QRect(w - 105, 5, 30, 25);
    m_resetHoleBtnRect  = QRect(w - 185, 5, 70, 25);
    m_toggleHoleBtnRect = QRect(w - 275, 5, 85, 25);

    painter.fillRect(m_toggleHoleBtnRect, m_holeEnabled ? QColor(0, 122, 255) : QColor(70, 70, 70));
    painter.drawText(m_toggleHoleBtnRect, Qt::AlignCenter, m_holeEnabled ? "Hole: ON" : "Hole: OFF");

    painter.fillRect(m_resetHoleBtnRect, QColor(60, 60, 60));
    painter.drawText(m_resetHoleBtnRect, Qt::AlignCenter, "Redraw");

    painter.fillRect(m_closeButtonRect, QColor(200, 40, 40));
    painter.drawText(m_closeButtonRect, Qt::AlignCenter, "X");

    painter.fillRect(m_maxButtonRect, QColor(60, 60, 60));
    painter.drawText(m_maxButtonRect, Qt::AlignCenter, (isMaximized() || isFullScreen()) ? "❐" : "□");

    painter.fillRect(m_minButtonRect, QColor(60, 60, 60));
    painter.drawText(m_minButtonRect, Qt::AlignCenter, "_");
}

QRect MainWindow::holeRect() const {
    return m_holeRect;
}