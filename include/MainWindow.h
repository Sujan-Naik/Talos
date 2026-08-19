#pragma once

#include <QWidget>
#include <QRect>
#include <QPoint>
#include <QRegion>

class CodeWidget;
class ChatWidget;
class AudioRecorder;
class WakeWordDetector;
class WhisperTranscriber;
class QStackedLayout;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    QRect holeRect() const;
    void chooseAudioDevice();

public slots:
    void toggleHole(bool enabled);
    void resetHole();
    void toggleCodeWidget();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSpeechCaptureFinished();

private:
    enum class Handle {
        None,
        WinTop, WinBottom, WinLeft, WinRight,
        WinTopLeft, WinTopRight, WinBottomLeft, WinBottomRight,
        HoleTop, HoleBottom, HoleLeft, HoleRight,
        HoleTopLeft, HoleTopRight, HoleBottomLeft, HoleBottomRight
    };

    enum class AppState {
        Listening,
        Triggered,
        Processing
    };

    void updateWidgetsGeometry();
    void updateClickThroughMask();
    void installWebEventFilters();
    Handle handleAt(const QPoint &pos) const;
    void updateCursorShape(const QPoint &pos);
    void toggleMaximize();
    void processWindowResize(const QPoint &globalDelta);
    void processHoleResize(const QPoint &globalDelta);

    QWidget *m_containerWidget = nullptr;
    QStackedLayout *m_stackedLayout = nullptr;
    ChatWidget *m_chatWidget = nullptr;
    CodeWidget *m_codeWidget = nullptr;
    AudioRecorder *m_audioRecorder = nullptr;
    WakeWordDetector *m_wakeWordDetector = nullptr;
    WhisperTranscriber *m_transcriber = nullptr;

    AppState m_appState = AppState::Listening;

    int m_bufferSize = 16;
    QRect m_holeRect;
    QRect m_previousHoleRect;
    bool m_holeEnabled = false;

    int m_handleSize = 16;
    int m_borderResizeWidth = 6;
    Handle m_activeHandle = Handle::None;
    bool m_isDraggingWindow = false;

    QPoint m_dragStartPos;
    QRect m_dragStartHoleRect;
    QRect m_dragStartWinGeometry;
    QPoint m_windowDragStartPos;
    QRect m_savedNormalGeometry;

    QRect m_titleBarRect;
    QRect m_closeButtonRect;
    QRect m_maxButtonRect;
    QRect m_minButtonRect;
    QRect m_toggleHoleBtnRect;
    QRect m_resetHoleBtnRect;
    QRect m_toggleCodeBtnRect;
};