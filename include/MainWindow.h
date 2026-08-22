#pragma once

#include <QRect>
#include <QWidget>

class InferenceService;
class ChatWidget;
class CodeWidget;
class AudioRecorder;
class WakeWordDetector;
class QStackedLayout;
class QMouseEvent;
class QEvent;
class QResizeEvent;
class QPaintEvent;

class MainWindow final : public QWidget
{
    Q_OBJECT

public:

    explicit MainWindow(
        InferenceService *inferenceService,
        QWidget *parent = nullptr
    );

    ~MainWindow() override;

    QRect holeRect() const;

protected:

    bool eventFilter(
        QObject *watched,
        QEvent *event
    ) override;

    void mousePressEvent(
        QMouseEvent *event
    ) override;

    void mouseDoubleClickEvent(
        QMouseEvent *event
    ) override;

    void mouseMoveEvent(
        QMouseEvent *event
    ) override;

    void mouseReleaseEvent(
        QMouseEvent *event
    ) override;

    void changeEvent(
        QEvent *event
    ) override;

    void resizeEvent(
        QResizeEvent *event
    ) override;

    void paintEvent(
        QPaintEvent *event
    ) override;

private:

    enum class AppState {
        Listening,
        Triggered,
        Processing
    };

    enum class Handle {
        None,

        WinTop,
        WinBottom,
        WinLeft,
        WinRight,

        WinTopLeft,
        WinTopRight,
        WinBottomLeft,
        WinBottomRight,

        HoleTop,
        HoleBottom,
        HoleLeft,
        HoleRight,

        HoleTopLeft,
        HoleTopRight,
        HoleBottomLeft,
        HoleBottomRight
    };

    void openModelDialog();

    void toggleCodeWidget();

    void installWebEventFilters();

    void chooseAudioDevice();

    void toggleHole(
        bool enabled
    );

    void resetHole();

    void onSpeechCaptureFinished();

    void updateWidgetsGeometry();

    void updateClickThroughMask();

    Handle handleAt(
        const QPoint &pos
    ) const;

    void updateCursorShape(
        const QPoint &pos
    );

    void toggleMaximize();

    void processWindowResize(
        const QPoint &globalDelta
    );

    void processHoleResize(
        const QPoint &globalDelta
    );

private:

    InferenceService *m_inference = nullptr;

    ChatWidget *m_chatWidget = nullptr;
    CodeWidget *m_codeWidget = nullptr;

    AudioRecorder *m_audioRecorder = nullptr;
    WakeWordDetector *m_wakeWordDetector = nullptr;

    QWidget *m_containerWidget = nullptr;
    QStackedLayout *m_stackedLayout = nullptr;

    QRect m_titleBarRect;

    QRect m_modelsButtonRect;
    QRect m_modelStatusRect;

    QRect m_toggleCodeBtnRect;
    QRect m_toggleHoleBtnRect;
    QRect m_resetHoleBtnRect;

    QRect m_minButtonRect;
    QRect m_maxButtonRect;
    QRect m_closeButtonRect;

    QRect m_holeRect;
    QRect m_previousHoleRect;

    QRect m_savedNormalGeometry;

    QPoint m_dragStartPos;
    QPoint m_dragStartHoleRectTopLeft;
    QPoint m_dragStartWinGeometryTopLeft;

    QPoint m_windowDragStartPos;

    QRect m_dragStartHoleRect;
    QRect m_dragStartWinGeometry;

    Handle m_activeHandle =
        Handle::None;

    AppState m_appState =
        AppState::Listening;

    bool m_holeEnabled = false;
    bool m_isDraggingWindow = false;

    int m_borderResizeWidth = 8;
    int m_handleSize = 8;
    int m_bufferSize = 2;
};