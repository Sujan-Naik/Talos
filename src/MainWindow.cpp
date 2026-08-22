#include "MainWindow.h"

#include "InferenceService.h"
#include "ChatWidget.h"
#include "CodeWidget.h"
#include "AudioRecorder.h"
#include "WakeWordDetector.h"
#include "ModelDialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEvent>
#include <QInputDialog>
#include <QMediaDevices>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QStackedLayout>
#include <QTimer>
#include <QToolTip>
#include <QVBoxLayout>
#include <QChildEvent>

#include <QFuture>
#include <QFutureWatcher>

#include <QtConcurrent/QtConcurrent>

#include <algorithm>

#include "ChatBackend.h"


MainWindow::MainWindow(
    InferenceService *inferenceService,
    QWidget *parent
)
    : QWidget(parent)
    , m_inference(inferenceService)
{
    setWindowFlags(
        Qt::FramelessWindowHint |
        Qt::WindowMinMaxButtonsHint
    );

    setAttribute(
        Qt::WA_TranslucentBackground,
        true
    );

    setMouseTracking(
        true
    );

    resize(
        800,
        600
    );

    m_savedNormalGeometry =
        QRect(
            100,
            100,
            800,
            600
        );

    m_holeRect =
        QRect(
            200,
            150,
            400,
            300
        );

    m_previousHoleRect =
        m_holeRect;

    // -------------------------------------------------------------------------
    // Main content container
    // -------------------------------------------------------------------------

    m_containerWidget =
        new QWidget(
            this
        );

    m_stackedLayout =
        new QStackedLayout(
            m_containerWidget
        );

    m_stackedLayout->setContentsMargins(
        0,
        0,
        0,
        0
    );

    m_chatWidget =
        new ChatWidget(
            m_inference,
            this
        );

    m_chatWidget->setHoleRect(
        m_holeRect,
        m_holeEnabled
    );

    m_codeWidget =
        new CodeWidget(
            this
        );

    m_stackedLayout->addWidget(
        m_chatWidget
    );

    m_stackedLayout->addWidget(
        m_codeWidget
    );

    m_stackedLayout->setCurrentWidget(
        m_chatWidget
    );

    installWebEventFilters();

    updateWidgetsGeometry();
    updateClickThroughMask();

    // -------------------------------------------------------------------------
    // Inference state updates
    // -------------------------------------------------------------------------

    if (m_inference) {

        connect(
            m_inference,
            &InferenceService::selectedLlmModelChanged,
            this,
            [this](const QString &) {
                QToolTip::hideText();
                update();
            }
        );

        connect(
            m_inference,
            &InferenceService::llmReady,
            this,
            [this]() {
                update();
            }
        );

        connect(
            m_inference,
            &InferenceService::llmError,
            this,
            [this](const QString &) {
                update();
            }
        );
    }

    // -------------------------------------------------------------------------
    // Audio / wake word
    // -------------------------------------------------------------------------

    m_audioRecorder =
        new AudioRecorder(
            this
        );

    const QString modelsDir =
        QDir(
            QCoreApplication::applicationDirPath()
        ).filePath(
            QStringLiteral(
                "models"
            )
        );

    m_wakeWordDetector =
        new WakeWordDetector(
            QDir(modelsDir).filePath(
                QStringLiteral(
                    "melspectrogram.onnx"
                )
            ),
            QDir(modelsDir).filePath(
                QStringLiteral(
                    "embedding_model.onnx"
                )
            ),
            QDir(modelsDir).filePath(
                QStringLiteral(
                    "hey_jarvis.onnx"
                )
            ),
            0.5f,
            this
        );

    connect(
        m_audioRecorder,
        &AudioRecorder::audioChunkReady,
        m_wakeWordDetector,
        &WakeWordDetector::processAudioChunk
    );

    connect(
        m_wakeWordDetector,
        &WakeWordDetector::wakeWordDetected,
        this,
        [this](float confidence) {

            qDebug()
                << "[Talos] Wake Word Detected!"
                << "Confidence:"
                << confidence;

            QTimer::singleShot(
                0,
                this,
                [this]() {

                    m_appState =
                        AppState::Triggered;

                    update();

                    m_audioRecorder
                        ->startBufferingSpeech();

                    QTimer::singleShot(
                        5000,
                        this,
                        &MainWindow::
                            onSpeechCaptureFinished
                    );
                }
            );
        }
    );

    const auto devices =
        m_audioRecorder
            ->availableInputDevices();

    if (!devices.isEmpty()) {

        m_audioRecorder
            ->setInputDevice(
                QMediaDevices::defaultAudioInput()
            );
    }
}


MainWindow::~MainWindow() = default;


void MainWindow::openModelDialog()
{
    if (!m_inference)
        return;

    ModelDialog dialog(
        m_inference,
        this
    );

    dialog.exec();

    QToolTip::hideText();
    update();
}


void MainWindow::toggleCodeWidget()
{
    if (
        !m_stackedLayout ||
        !m_chatWidget ||
        !m_codeWidget
    ) {
        return;
    }

    if (
        m_stackedLayout->currentWidget() ==
        m_codeWidget
    ) {
        m_stackedLayout->setCurrentWidget(
            m_chatWidget
        );
    } else {
        m_stackedLayout->setCurrentWidget(
            m_codeWidget
        );
    }

    update();
}


void MainWindow::installWebEventFilters()
{
    if (!m_chatWidget)
        return;

    m_chatWidget->installEventFilter(
        this
    );

    if (
        auto *view =
            m_chatWidget->webView()
    ) {
        view->installEventFilter(
            this
        );

        if (view->focusProxy()) {
            view->focusProxy()
                ->installEventFilter(
                    this
                );
        }
    }
}


bool MainWindow::eventFilter(
    QObject *watched,
    QEvent *event
)
{
    if (
        event->type() ==
        QEvent::ChildAdded
    ) {
        auto *childEvent =
            static_cast<QChildEvent *>(
                event
            );

        if (childEvent->child()) {
            childEvent->child()
                ->installEventFilter(
                    this
                );
        }
    }

    if (
        event->type() ==
            QEvent::MouseButtonPress ||
        event->type() ==
            QEvent::MouseMove ||
        event->type() ==
            QEvent::MouseButtonRelease
    ) {
        auto *mouseEvent =
            static_cast<QMouseEvent *>(
                event
            );

        const QPoint globalPos =
            mouseEvent
                ->globalPosition()
                .toPoint();

        const QPoint localPos =
            mapFromGlobal(
                globalPos
            );

        const Handle handle =
            handleAt(
                localPos
            );

        if (
            event->type() ==
            QEvent::MouseMove
        ) {

            if (
                m_activeHandle !=
                Handle::None
            ) {
                QMouseEvent syntheticEvent(
                    QEvent::MouseMove,
                    QPointF(localPos),
                    QPointF(globalPos),
                    mouseEvent->button(),
                    mouseEvent->buttons(),
                    mouseEvent->modifiers()
                );

                mouseMoveEvent(
                    &syntheticEvent
                );

                return true;

            } else {

                updateCursorShape(
                    localPos
                );

                if (
                    handle !=
                    Handle::None
                ) {
                    return false;
                }
            }

        } else if (
            event->type() ==
            QEvent::MouseButtonPress
        ) {

            if (
                mouseEvent->button() ==
                    Qt::LeftButton &&
                handle !=
                    Handle::None
            ) {
                QMouseEvent syntheticEvent(
                    QEvent::MouseButtonPress,
                    QPointF(localPos),
                    QPointF(globalPos),
                    mouseEvent->button(),
                    mouseEvent->buttons(),
                    mouseEvent->modifiers()
                );

                mousePressEvent(
                    &syntheticEvent
                );

                return true;
            }

        } else if (
            event->type() ==
            QEvent::MouseButtonRelease
        ) {

            if (
                mouseEvent->button() ==
                    Qt::LeftButton &&
                m_activeHandle !=
                    Handle::None
            ) {
                QMouseEvent syntheticEvent(
                    QEvent::MouseButtonRelease,
                    QPointF(localPos),
                    QPointF(globalPos),
                    mouseEvent->button(),
                    mouseEvent->buttons(),
                    mouseEvent->modifiers()
                );

                mouseReleaseEvent(
                    &syntheticEvent
                );

                return true;
            }
        }
    }

    return QWidget::eventFilter(
        watched,
        event
    );
}


void MainWindow::chooseAudioDevice()
{
    if (!m_audioRecorder)
        return;

    const auto devices =
        m_audioRecorder
            ->availableInputDevices();

    if (devices.isEmpty())
        return;

    QStringList names;

    int currentIndex =
        0;

    const QAudioDevice current =
        m_audioRecorder
            ->currentDevice();

    for (
        int i = 0;
        i < devices.size();
        ++i
    ) {
        names <<
            devices[i]
                .description();

        if (
            !current.isNull() &&
            devices[i].id() ==
                current.id()
        ) {
            currentIndex =
                i;
        }
    }

    bool ok =
        false;

    const QString chosen =
        QInputDialog::getItem(
            this,
            QStringLiteral(
                "Select Microphone"
            ),
            QStringLiteral(
                "Audio input device:"
            ),
            names,
            currentIndex,
            false,
            &ok
        );

    if (
        ok &&
        !chosen.isEmpty()
    ) {
        for (
            const QAudioDevice &device :
            devices
        ) {
            if (
                device.description() ==
                chosen
            ) {
                m_audioRecorder
                    ->setInputDevice(
                        device
                    );

                break;
            }
        }
    }
}


void MainWindow::toggleHole(
    bool enabled
)
{
    m_holeEnabled =
        enabled;

    updateWidgetsGeometry();
    updateClickThroughMask();

    if (!m_holeEnabled)
        clearMask();

    if (m_chatWidget) {

        m_chatWidget
            ->setHoleEnabled(
                m_holeEnabled
            );

        m_chatWidget
            ->setHoleRect(
                m_holeRect,
                m_holeEnabled
            );

        m_chatWidget->update();
    }

    update();
}


void MainWindow::resetHole()
{
    const int w =
        width();

    const int h =
        height();

    const int titleBarHeight =
        35;

    m_previousHoleRect =
        m_holeRect;

    m_holeRect =
        QRect(
            (w - 400) / 2,
            std::max(
                titleBarHeight + 10,
                (h - 300) / 2
            ),
            400,
            300
        );

    m_holeEnabled =
        true;

    updateWidgetsGeometry();
    updateClickThroughMask();

    if (m_chatWidget) {

        m_chatWidget
            ->setHoleEnabled(
                true
            );

        m_chatWidget
            ->setHoleRect(
                m_holeRect,
                m_holeEnabled
            );

        m_chatWidget->update();
    }

    update();
}


void MainWindow::onSpeechCaptureFinished()
{
    m_appState =
        AppState::Processing;

    update();

    const std::vector<float> pcmData =
        m_audioRecorder
            ->stopBufferingSpeech();

    if (
        pcmData.empty() ||
        !m_inference ||
        !m_inference->isSttReady()
    ) {
        m_wakeWordDetector->reset();

        m_appState =
            AppState::Listening;

        update();

        m_audioRecorder->startListening();

        return;
    }

    auto *watcher =
        new QFutureWatcher<QString>(
            this
        );

    connect(
        watcher,
        &QFutureWatcher<QString>::finished,
        this,
        [this, watcher]() {

            const QString transcribedText =
                watcher->result();

            watcher->deleteLater();

            if (
                !transcribedText
                    .trimmed()
                    .isEmpty() &&
                m_chatWidget &&
                m_chatWidget->backend()
            ) {
                m_chatWidget
                    ->backend()
                    ->onUserSendMessage(
                        transcribedText
                    );
            }

            m_wakeWordDetector->reset();

            m_appState =
                AppState::Listening;

            update();

            m_audioRecorder
                ->startListening();
        }
    );

    const auto future =
        QtConcurrent::run(
            [this, pcmData]() {

                return m_inference
                    ? m_inference
                        ->transcribe(
                            pcmData
                        )
                    : QString();
            }
        );

    watcher->setFuture(
        future
    );
}


void MainWindow::updateWidgetsGeometry()
{
    const int titleBarHeight =
        35;

    const QRect contentArea(
        0,
        titleBarHeight,
        width(),
        height() -
            titleBarHeight
    );

    if (m_containerWidget)
        m_containerWidget
            ->setGeometry(
                contentArea
            );

    if (m_chatWidget) {
        m_chatWidget
            ->setHoleRect(
                m_holeRect,
                m_holeEnabled
            );
    }
}


void MainWindow::updateClickThroughMask()
{
    const int w =
        width();

    const int h =
        height();

    if (
        m_holeEnabled &&
        m_holeRect.isValid()
    ) {
        const QRegion fullRegion(
            0,
            0,
            w,
            h
        );

        const QRect passthroughRect =
            m_holeRect.adjusted(
                m_bufferSize,
                m_bufferSize,
                -m_bufferSize,
                -m_bufferSize
            );

        if (
            passthroughRect.isValid() &&
            passthroughRect.width() > 0 &&
            passthroughRect.height() > 0
        ) {
            const QRegion holeRegion(
                passthroughRect
            );

            const QRegion interactiveMask =
                fullRegion.subtracted(
                    holeRegion
                );

            clearMask();

            setMask(
                interactiveMask
            );

        } else {

            clearMask();
        }

    } else {

        clearMask();
    }
}


MainWindow::Handle MainWindow::handleAt(
    const QPoint &pos
) const
{
    if (
        !isMaximized() &&
        !isFullScreen()
    ) {
        const int bw =
            m_borderResizeWidth;

        const int w =
            width();

        const int h =
            height();

        const bool top =
            pos.y() <= bw;

        const bool bottom =
            pos.y() >= h - bw;

        const bool left =
            pos.x() <= bw;

        const bool right =
            pos.x() >= w - bw;

        if (top && left)
            return Handle::WinTopLeft;

        if (top && right)
            return Handle::WinTopRight;

        if (bottom && left)
            return Handle::WinBottomLeft;

        if (bottom && right)
            return Handle::WinBottomRight;

        if (top)
            return Handle::WinTop;

        if (bottom)
            return Handle::WinBottom;

        if (left)
            return Handle::WinLeft;

        if (right)
            return Handle::WinRight;
    }

    if (!m_holeEnabled)
        return Handle::None;

    const int hs =
        m_handleSize;

    const QRect hole =
        m_holeRect;

    const QRect tl(
        hole.left() - hs / 2,
        hole.top() - hs / 2,
        hs * 2,
        hs * 2
    );

    const QRect tr(
        hole.right() - hs / 2,
        hole.top() - hs / 2,
        hs * 2,
        hs * 2
    );

    const QRect bl(
        hole.left() - hs / 2,
        hole.bottom() - hs / 2,
        hs * 2,
        hs * 2
    );

    const QRect br(
        hole.right() - hs / 2,
        hole.bottom() - hs / 2,
        hs * 2,
        hs * 2
    );

    if (tl.contains(pos))
        return Handle::HoleTopLeft;

    if (tr.contains(pos))
        return Handle::HoleTopRight;

    if (bl.contains(pos))
        return Handle::HoleBottomLeft;

    if (br.contains(pos))
        return Handle::HoleBottomRight;

    const QRect l(
        hole.left() - hs / 2,
        hole.top() + hs,
        hs * 2,
        hole.height() - 2 * hs
    );

    const QRect r(
        hole.right() - hs / 2,
        hole.top() + hs,
        hs * 2,
        hole.height() - 2 * hs
    );

    const QRect t(
        hole.left() + hs,
        hole.top() - hs / 2,
        hole.width() - 2 * hs,
        hs * 2
    );

    const QRect b(
        hole.left() + hs,
        hole.bottom() - hs / 2,
        hole.width() - 2 * hs,
        hs * 2
    );

    if (l.contains(pos))
        return Handle::HoleLeft;

    if (r.contains(pos))
        return Handle::HoleRight;

    if (t.contains(pos))
        return Handle::HoleTop;

    if (b.contains(pos))
        return Handle::HoleBottom;

    return Handle::None;
}


void MainWindow::updateCursorShape(
    const QPoint &pos
)
{
    const Handle handle =
        handleAt(pos);

    switch (handle) {

    case Handle::HoleTopLeft:
    case Handle::HoleBottomRight:
    case Handle::WinTopLeft:
    case Handle::WinBottomRight:
        setCursor(
            Qt::SizeFDiagCursor
        );
        break;

    case Handle::HoleTopRight:
    case Handle::HoleBottomLeft:
    case Handle::WinTopRight:
    case Handle::WinBottomLeft:
        setCursor(
            Qt::SizeBDiagCursor
        );
        break;

    case Handle::HoleLeft:
    case Handle::HoleRight:
    case Handle::WinLeft:
    case Handle::WinRight:
        setCursor(
            Qt::SizeHorCursor
        );
        break;

    case Handle::HoleTop:
    case Handle::HoleBottom:
    case Handle::WinTop:
    case Handle::WinBottom:
        setCursor(
            Qt::SizeVerCursor
        );
        break;

    default:
        setCursor(
            Qt::ArrowCursor
        );
        break;
    }
}


void MainWindow::toggleMaximize()
{
    if (
        isMaximized() ||
        isFullScreen()
    ) {
        showNormal();

        if (
            m_savedNormalGeometry
                .isValid()
        ) {
            setGeometry(
                m_savedNormalGeometry
            );
        }

    } else {

        m_savedNormalGeometry =
            geometry();

        showMaximized();
    }
}


void MainWindow::mousePressEvent(
    QMouseEvent *event
)
{
    if (
        event->button() ==
            Qt::RightButton &&
        m_titleBarRect.contains(
            event->pos()
        )
    ) {
        chooseAudioDevice();
        return;
    }

    if (
        event->button() ==
        Qt::LeftButton
    ) {
        if (
            m_closeButtonRect.contains(
                event->pos()
            )
        ) {
            QToolTip::hideText();
            close();
            return;
        }

        if (
            m_maxButtonRect.contains(
                event->pos()
            )
        ) {
            QToolTip::hideText();
            toggleMaximize();
            return;
        }

        if (
            m_minButtonRect.contains(
                event->pos()
            )
        ) {
            QToolTip::hideText();
            showMinimized();
            return;
        }

        if (
            m_modelsButtonRect.contains(
                event->pos()
            )
        ) {
            QToolTip::hideText();
            openModelDialog();
            return;
        }

        if (
            m_toggleHoleBtnRect.contains(
                event->pos()
            )
        ) {
            QToolTip::hideText();
            toggleHole(
                !m_holeEnabled
            );
            return;
        }

        if (
            m_resetHoleBtnRect.contains(
                event->pos()
            )
        ) {
            QToolTip::hideText();
            resetHole();
            return;
        }

        if (
            m_toggleCodeBtnRect.contains(
                event->pos()
            )
        ) {
            QToolTip::hideText();
            toggleCodeWidget();
            return;
        }

        m_activeHandle =
            handleAt(
                event->pos()
            );

        if (
            m_activeHandle !=
            Handle::None
        ) {
            QToolTip::hideText();

            m_dragStartPos =
                event->globalPosition()
                    .toPoint();

            m_dragStartHoleRect =
                m_holeRect;

            m_dragStartWinGeometry =
                geometry();

        } else if (
            m_titleBarRect.contains(
                event->pos()
            )
        ) {
            m_isDraggingWindow =
                true;

            m_windowDragStartPos =
                event->globalPosition()
                    .toPoint()
                -
                frameGeometry()
                    .topLeft();
        }
    }
}


void MainWindow::mouseDoubleClickEvent(
    QMouseEvent *event
)
{
    if (
        event->button() ==
            Qt::LeftButton &&
        m_titleBarRect.contains(
            event->pos()
        )
    ) {
        if (
            !m_closeButtonRect.contains(
                event->pos()
            ) &&
            !m_maxButtonRect.contains(
                event->pos()
            ) &&
            !m_minButtonRect.contains(
                event->pos()
            ) &&
            !m_modelsButtonRect.contains(
                event->pos()
            ) &&
            !m_toggleHoleBtnRect.contains(
                event->pos()
            ) &&
            !m_resetHoleBtnRect.contains(
                event->pos()
            ) &&
            !m_toggleCodeBtnRect.contains(
                event->pos()
            )
        ) {
            toggleMaximize();
        }
    }
}


void MainWindow::processWindowResize(
    const QPoint &globalDelta
)
{
    QRect newWindow =
        m_dragStartWinGeometry;

    switch (m_activeHandle) {

    case Handle::WinTopLeft:
        newWindow.setTopLeft(
            m_dragStartWinGeometry.topLeft()
            + globalDelta
        );
        break;

    case Handle::WinTopRight:
        newWindow.setTopRight(
            m_dragStartWinGeometry.topRight()
            + globalDelta
        );
        break;

    case Handle::WinBottomLeft:
        newWindow.setBottomLeft(
            m_dragStartWinGeometry.bottomLeft()
            + globalDelta
        );
        break;

    case Handle::WinBottomRight:
        newWindow.setBottomRight(
            m_dragStartWinGeometry.bottomRight()
            + globalDelta
        );
        break;

    case Handle::WinTop:
        newWindow.setTop(
            m_dragStartWinGeometry.top()
            + globalDelta.y()
        );
        break;

    case Handle::WinBottom:
        newWindow.setBottom(
            m_dragStartWinGeometry.bottom()
            + globalDelta.y()
        );
        break;

    case Handle::WinLeft:
        newWindow.setLeft(
            m_dragStartWinGeometry.left()
            + globalDelta.x()
        );
        break;

    case Handle::WinRight:
        newWindow.setRight(
            m_dragStartWinGeometry.right()
            + globalDelta.x()
        );
        break;

    default:
        break;
    }

    if (
        newWindow.width() >= 200 &&
        newWindow.height() >= 150
    ) {
        setGeometry(
            newWindow
        );

        m_savedNormalGeometry =
            newWindow;
    }
}


void MainWindow::processHoleResize(
    const QPoint &globalDelta
)
{
    QRect newHole =
        m_dragStartHoleRect;

    switch (m_activeHandle) {

    case Handle::HoleTopLeft:
        newHole.setTopLeft(
            m_dragStartHoleRect.topLeft()
            + globalDelta
        );
        break;

    case Handle::HoleTopRight:
        newHole.setTopRight(
            m_dragStartHoleRect.topRight()
            + globalDelta
        );
        break;

    case Handle::HoleBottomLeft:
        newHole.setBottomLeft(
            m_dragStartHoleRect.bottomLeft()
            + globalDelta
        );
        break;

    case Handle::HoleBottomRight:
        newHole.setBottomRight(
            m_dragStartHoleRect.bottomRight()
            + globalDelta
        );
        break;

    case Handle::HoleLeft:
        newHole.setLeft(
            m_dragStartHoleRect.left()
            + globalDelta.x()
        );
        break;

    case Handle::HoleRight:
        newHole.setRight(
            m_dragStartHoleRect.right()
            + globalDelta.x()
        );
        break;

    case Handle::HoleTop:
        newHole.setTop(
            m_dragStartHoleRect.top()
            + globalDelta.y()
        );
        break;

    case Handle::HoleBottom:
        newHole.setBottom(
            m_dragStartHoleRect.bottom()
            + globalDelta.y()
        );
        break;

    default:
        break;
    }

    const int minSize =
        100;

    const int titleBarHeight =
        35;

    if (
        newHole.width() >= minSize &&
        newHole.height() >= minSize &&
        newHole.left() >= 0 &&
        newHole.top() >= titleBarHeight &&
        newHole.right() <= width() &&
        newHole.bottom() <= height()
    ) {
        m_previousHoleRect =
            m_holeRect;

        m_holeRect =
            newHole;

        updateWidgetsGeometry();
        updateClickThroughMask();

        if (m_chatWidget) {
            m_chatWidget
                ->setHoleRect(
                    m_holeRect,
                    m_holeEnabled
                );
        }

        update();
    }
}


void MainWindow::mouseMoveEvent(
    QMouseEvent *event
)
{
    // -------------------------------------------------------------------------
    // Models tooltip
    // -------------------------------------------------------------------------

    if (
        m_modelsButtonRect.contains(
            event->pos()
        )
    ) {
        QString modelTooltip =
            QStringLiteral(
                "No local model selected"
            );

        if (m_inference) {

            const auto model =
                m_inference
                    ->selectedLlmModel();

            if (!model.id.isEmpty()) {

                modelTooltip =
                    model.displayName;

                if (modelTooltip.isEmpty())
                    modelTooltip =
                        model.id;

                if (
                    !model.quantization.isEmpty()
                ) {
                    modelTooltip +=
                        QStringLiteral(
                            "\nQuantization: %1"
                        ).arg(
                            model.quantization
                        );
                }

                if (
                    m_inference
                        ->isLlmReady()
                ) {
                    modelTooltip +=
                        QStringLiteral(
                            "\nStatus: Ready"
                        );
                } else {
                    modelTooltip +=
                        QStringLiteral(
                            "\nStatus: Starting / not ready"
                        );
                }
            }
        }

        QToolTip::showText(
            event->globalPosition().toPoint(),
            modelTooltip,
            this,
            m_modelsButtonRect
        );

    } else {
        QToolTip::hideText();
    }

    // -------------------------------------------------------------------------
    // Window dragging
    // -------------------------------------------------------------------------

    if (m_isDraggingWindow) {

        if (
            isMaximized() ||
            isFullScreen()
        ) {
            const QPoint globalPos =
                event->globalPosition()
                    .toPoint();

            const int dragDistanceY =
                globalPos.y() -
                m_windowDragStartPos.y();

            if (
                dragDistanceY > 5 ||
                globalPos.y() > 10
            ) {
                const qreal relativeX =
                    static_cast<qreal>(
                        globalPos.x()
                    ) /
                    width();

                showNormal();

                const int newWidth =
                    m_savedNormalGeometry
                        .isValid()
                        ? m_savedNormalGeometry.width()
                        : 800;

                const int newHeight =
                    m_savedNormalGeometry
                        .isValid()
                        ? m_savedNormalGeometry.height()
                        : 600;

                const int newX =
                    globalPos.x() -
                    static_cast<int>(
                        newWidth *
                        relativeX
                    );

                const int newY =
                    globalPos.y() -
                    15;

                setGeometry(
                    newX,
                    newY,
                    newWidth,
                    newHeight
                );

                m_windowDragStartPos =
                    QPoint(
                        static_cast<int>(
                            newWidth *
                            relativeX
                        ),
                        15
                    );
            }

            return;
        }

        move(
            event->globalPosition()
                .toPoint()
            -
            m_windowDragStartPos
        );

        return;
    }

    if (
        m_activeHandle ==
        Handle::None
    ) {
        updateCursorShape(
            event->pos()
        );

        return;
    }

    const QPoint globalDelta =
        event->globalPosition()
            .toPoint()
        -
        m_dragStartPos;

    if (
        m_activeHandle >=
            Handle::WinTop &&
        m_activeHandle <=
            Handle::WinBottomRight
    ) {
        processWindowResize(
            globalDelta
        );
    } else {
        processHoleResize(
            globalDelta
        );
    }
}


void MainWindow::mouseReleaseEvent(
    QMouseEvent *event
)
{
    if (
        event->button() ==
        Qt::LeftButton
    ) {
        m_activeHandle =
            Handle::None;

        m_isDraggingWindow =
            false;

        if (
            !isMaximized() &&
            !isFullScreen()
        ) {
            m_savedNormalGeometry =
                geometry();
        }

        updateCursorShape(
            event->pos()
        );
    }
}


void MainWindow::changeEvent(
    QEvent *event
)
{
    QWidget::changeEvent(
        event
    );

    if (
        event->type() ==
        QEvent::WindowStateChange
    ) {
        QToolTip::hideText();

        updateWidgetsGeometry();
        updateClickThroughMask();
    }
}


void MainWindow::resizeEvent(
    QResizeEvent *event
)
{
    QWidget::resizeEvent(
        event
    );

    QToolTip::hideText();

    if (
        !isMaximized() &&
        !isFullScreen() &&
        !m_isDraggingWindow
    ) {
        m_savedNormalGeometry =
            geometry();
    }

    updateWidgetsGeometry();
    updateClickThroughMask();

    if (m_chatWidget) {

        m_chatWidget
            ->setHoleRect(
                m_holeRect,
                m_holeEnabled
            );

        m_chatWidget->update();
    }
}


void MainWindow::paintEvent(
    QPaintEvent *event
)
{
    Q_UNUSED(event);

    QPainter painter(
        this
    );

    painter.setRenderHint(
        QPainter::Antialiasing
    );

    const int w =
        width();

    m_titleBarRect =
        QRect(
            0,
            0,
            w,
            35
        );

    painter.fillRect(
        m_titleBarRect,
        QColor(
            30,
            30,
            30,
            240
        )
    );

    QColor indicatorColor;
    QString statusText;

    if (
        m_appState ==
        AppState::Listening
    ) {

        indicatorColor =
            QColor(
                46,
                204,
                113
            );

        statusText =
            QStringLiteral(
                "Listening..."
            );

    } else if (
        m_appState ==
        AppState::Triggered
    ) {

        indicatorColor =
            QColor(
                241,
                196,
                15
            );

        statusText =
            QStringLiteral(
                "Recording speech..."
            );

    } else {

        indicatorColor =
            QColor(
                52,
                152,
                219
            );

        statusText =
            QStringLiteral(
                "Transcribing..."
            );
    }

    painter.setBrush(
        indicatorColor
    );

    painter.setPen(
        Qt::NoPen
    );

    painter.drawEllipse(
        QPointF(
            20,
            17.5
        ),
        5,
        5
    );

    painter.setPen(
        Qt::white
    );

    painter.drawText(
        QRect(
            35,
            0,
            220,
            35
        ),
        Qt::AlignVCenter |
            Qt::AlignLeft,
        QStringLiteral(
            "Talos Overlay — "
        ) +
            statusText
    );

    // -------------------------------------------------------------------------
    // Toolbar geometry
    // -------------------------------------------------------------------------

    m_modelsButtonRect =
        QRect(
            w - 420,
            5,
            70,
            25
        );

    m_toggleCodeBtnRect =
        QRect(
            w - 345,
            5,
            65,
            25
        );

    m_toggleHoleBtnRect =
        QRect(
            w - 275,
            5,
            85,
            25
        );

    m_resetHoleBtnRect =
        QRect(
            w - 185,
            5,
            70,
            25
        );

    m_minButtonRect =
        QRect(
            w - 105,
            5,
            30,
            25
        );

    m_maxButtonRect =
        QRect(
            w - 70,
            5,
            30,
            25
        );

    m_closeButtonRect =
        QRect(
            w - 35,
            5,
            30,
            25
        );

    // -------------------------------------------------------------------------
    // Code button
    // -------------------------------------------------------------------------

    const bool codeVisible =
        m_stackedLayout &&
        (
            m_stackedLayout
                ->currentWidget() ==
            m_codeWidget
        );

    painter.fillRect(
        m_toggleCodeBtnRect,
        codeVisible
            ? QColor(
                  0,
                  122,
                  255
              )
            : QColor(
                  70,
                  70,
                  70
              )
    );

    painter.setPen(
        Qt::white
    );

    painter.drawText(
        m_toggleCodeBtnRect,
        Qt::AlignCenter,
        QStringLiteral(
            "Code"
        )
    );

    // -------------------------------------------------------------------------
    // Hole button
    // -------------------------------------------------------------------------

    painter.fillRect(
        m_toggleHoleBtnRect,
        m_holeEnabled
            ? QColor(
                  0,
                  122,
                  255
              )
            : QColor(
                  70,
                  70,
                  70
              )
    );

    painter.setPen(
        Qt::white
    );

    painter.drawText(
        m_toggleHoleBtnRect,
        Qt::AlignCenter,
        m_holeEnabled
            ? QStringLiteral(
                  "Hole: ON"
              )
            : QStringLiteral(
                  "Hole: OFF"
              )
    );

    // -------------------------------------------------------------------------
    // Redraw button
    // -------------------------------------------------------------------------

    painter.fillRect(
        m_resetHoleBtnRect,
        QColor(
            60,
            60,
            60
        )
    );

    painter.setPen(
        Qt::white
    );

    painter.drawText(
        m_resetHoleBtnRect,
        Qt::AlignCenter,
        QStringLiteral(
            "Redraw"
        )
    );

    // -------------------------------------------------------------------------
    // Models button
    // -------------------------------------------------------------------------

    QColor modelButtonColor;

    if (
        m_inference &&
        m_inference->isLlmReady()
    ) {
        modelButtonColor =
            QColor(
                46,
                204,
                113
            );
    } else {
        modelButtonColor =
            QColor(
                200,
                40,
                40
            );
    }

    painter.fillRect(
        m_modelsButtonRect,
        modelButtonColor
    );

    painter.setPen(
        Qt::white
    );

    painter.drawText(
        m_modelsButtonRect,
        Qt::AlignCenter,
        QStringLiteral(
            "Models"
        )
    );
    // -------------------------------------------------------------------------
    // Window controls
    // -------------------------------------------------------------------------

    painter.fillRect(
        m_closeButtonRect,
        QColor(
            200,
            40,
            40
        )
    );

    painter.setPen(
        Qt::white
    );

    painter.drawText(
        m_closeButtonRect,
        Qt::AlignCenter,
        QStringLiteral(
            "X"
        )
    );

    painter.fillRect(
        m_maxButtonRect,
        QColor(
            60,
            60,
            60
        )
    );

    painter.drawText(
        m_maxButtonRect,
        Qt::AlignCenter,
        (
            isMaximized() ||
            isFullScreen()
        )
            ? QStringLiteral("❐")
            : QStringLiteral("□")
    );

    painter.fillRect(
        m_minButtonRect,
        QColor(
            60,
            60,
            60
        )
    );

    painter.drawText(
        m_minButtonRect,
        Qt::AlignCenter,
        QStringLiteral(
            "_"
        )
    );
}


QRect MainWindow::holeRect() const
{
    return m_holeRect;
}