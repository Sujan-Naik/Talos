#include "../../include/window/MainWindow.h"
#include "../../include/ChatWidget.h"
#include "../../include/AudioRecorder.h"
#include "../../include/WakeWordDetector.h"
#include "../../include/WhisperTranscriber.h"

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

        // Defer everything to the next event-loop iteration
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
    } else {
        qWarning() << "[Talos] No audio input devices found at startup";
    }
}

void MainWindow::chooseAudioDevice() {
    const auto devices = m_audioRecorder->availableInputDevices();
    if (devices.isEmpty()) {
        qWarning() << "[Talos] No audio input devices found";
        return;
    }

    QStringList names;
    int currentIndex = 0;
    QAudioDevice current = m_audioRecorder->currentDevice();

    for (int i = 0; i < devices.size(); ++i) {
        names << devices[i].description();
        if (!current.isNull() && devices[i].id() == current.id())
            currentIndex = i;
    }

    bool ok = false;
    QString chosen = QInputDialog::getItem(
            this,
            "Select Microphone",
            "Audio input device:",
            names,
            currentIndex,
            false,
            &ok
    );

    if (!ok || chosen.isEmpty())
        return;

    for (const QAudioDevice &dev : devices) {
        if (dev.description() == chosen) {
            m_audioRecorder->setInputDevice(dev);
            qDebug() << "[Talos] User selected microphone:" << chosen;
            break;
        }
    }
}

void MainWindow::onSpeechCaptureFinished() {
    qDebug() << "[Talos] onSpeechCaptureFinished entered";
    m_appState = StateProcessing;
    update();

    qDebug() << "[Talos] calling stopBufferingSpeech...";
    std::vector<float> pcmData = m_audioRecorder->stopBufferingSpeech();
    qDebug() << "[Talos] stopBufferingSpeech returned, samples:" << pcmData.size();

    if (pcmData.empty()) {
        qDebug() << "[Talos] Captured audio buffer was empty.";
        m_wakeWordDetector->reset();
        m_appState = StateListening;
        update();
        m_audioRecorder->startListening();
        return;
    }

    qDebug() << "[Talos] starting QtConcurrent for Whisper...";

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        qDebug() << "[Talos] Whisper future finished";
        QString transcribedText = watcher->result();
        watcher->deleteLater();

        if (!transcribedText.trimmed().isEmpty()) {
            qDebug() << "[Talos] Transcription result:" << transcribedText;
            m_chatWidget->appendMessageAsUser(transcribedText);
            m_chatWidget->sendApiRequest();
        } else {
            qDebug() << "[Talos] Could not recognize speech.";
        }

        m_wakeWordDetector->reset();
        m_appState = StateListening;
        update();
        m_audioRecorder->startListening();
    });

    QFuture<QString> future = QtConcurrent::run([pcmData, this]() {
        qDebug() << "[Talos] Whisper thread started, samples:" << pcmData.size();
        QString result = m_transcriber->transcribe(pcmData);
        qDebug() << "[Talos] Whisper thread finished";
        return result;
    });
    watcher->setFuture(future);
    qDebug() << "[Talos] future set, waiting...";
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

        if (top && left) return Handle::WinTopLeft;
        if (top && right) return Handle::WinTopRight;
        if (bottom && left) return Handle::WinBottomLeft;
        if (bottom && right) return Handle::WinBottomRight;
        if (top) return Handle::WinTop;
        if (bottom) return Handle::WinBottom;
        if (left) return Handle::WinLeft;
        if (right) return Handle::WinRight;
    }

    int hs = m_handleSize;

    QRect tl(m_holeRect.left() - hs/2, m_holeRect.top() - hs/2, hs, hs);
    QRect tr(m_holeRect.right() - hs/2, m_holeRect.top() - hs/2, hs, hs);
    QRect bl(m_holeRect.left() - hs/2, m_holeRect.bottom() - hs/2, hs, hs);
    QRect br(m_holeRect.right() - hs/2, m_holeRect.bottom() - hs/2, hs, hs);

    if (tl.contains(pos)) return Handle::HoleTopLeft;
    if (tr.contains(pos)) return Handle::HoleTopRight;
    if (bl.contains(pos)) return Handle::HoleBottomLeft;
    if (br.contains(pos)) return Handle::HoleBottomRight;

    QRect l(m_holeRect.left() - hs/2, m_holeRect.top() + hs/2, hs, m_holeRect.height() - hs);
    QRect r(m_holeRect.right() - hs/2, m_holeRect.top() + hs/2, hs, m_holeRect.height() - hs);
    QRect t(m_holeRect.left() + hs/2, m_holeRect.top() - hs/2, m_holeRect.width() - hs, hs);
    QRect b(m_holeRect.left() + hs/2, m_holeRect.bottom() - hs/2, m_holeRect.width() - hs, hs);

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
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
}

void MainWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::RightButton && m_titleBarRect.contains(event->pos())) {
        chooseAudioDevice();
        return;
    }

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
        if (m_activeHandle != Handle::None) {
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
        }
        return;
    }

    QRect newHole = m_dragStartHoleRect;

    switch (m_activeHandle) {
        case Handle::HoleTopLeft: newHole.setTopLeft(m_dragStartHoleRect.topLeft() + globalDelta); break;
        case Handle::HoleTopRight: newHole.setTopRight(m_dragStartHoleRect.topRight() + globalDelta); break;
        case Handle::HoleBottomLeft: newHole.setBottomLeft(m_dragStartHoleRect.bottomLeft() + globalDelta); break;
        case Handle::HoleBottomRight: newHole.setBottomRight(m_dragStartHoleRect.bottomRight() + globalDelta); break;
        case Handle::HoleLeft: newHole.setLeft(m_dragStartHoleRect.left() + globalDelta.x()); break;
        case Handle::HoleRight: newHole.setRight(m_dragStartHoleRect.right() + globalDelta.x()); break;
        case Handle::HoleTop: newHole.setTop(m_dragStartHoleRect.top() + globalDelta.y()); break;
        case Handle::HoleBottom: newHole.setBottom(m_dragStartHoleRect.bottom() + globalDelta.y()); break;
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
        m_activeHandle = Handle::None;
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
    painter.drawText(QRect(35, 0, 300, 35), Qt::AlignVCenter | Qt::AlignLeft, "Talos Overlay — " + statusText);

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

QRect MainWindow::holeRect()  {
    return m_holeRect;
}
