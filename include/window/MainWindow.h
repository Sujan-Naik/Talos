#pragma once

#include <QWidget>
#include <QRect>
#include <QPoint>
#include <QString>
#include <QInputDialog>
#include <QDebug>
#include "../ChatWidget.h"
#include "../AudioRecorder.h"
#include "../WakeWordDetector.h"
#include "../WhisperTranscriber.h"

class MainWindow : public QWidget {
Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    QRect holeRect();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    enum class Handle {
        None,
        HoleTopLeft, HoleTopRight, HoleBottomLeft, HoleBottomRight,
        HoleLeft, HoleRight, HoleTop, HoleBottom,
        WinTop, WinBottom, WinLeft, WinRight,
        WinTopLeft, WinTopRight, WinBottomLeft, WinBottomRight
    };

    enum AppState {
        StateListening,
        StateTriggered,
        StateProcessing
    };

    void updateChatGeometry();
    void updateClickThroughMask();
    Handle handleAt(const QPoint &pos) const;
    void updateCursorShape(const QPoint &pos);
    void toggleMaximize();
    void onSpeechCaptureFinished();
    void chooseAudioDevice();

    QRect m_holeRect;
    QRect m_titleBarRect;
    QRect m_closeButtonRect;
    QRect m_maxButtonRect;
    QRect m_minButtonRect;

    ChatWidget *m_chatWidget = nullptr;
    AudioRecorder *m_audioRecorder = nullptr;
    WakeWordDetector *m_wakeWordDetector = nullptr;
    WhisperTranscriber *m_transcriber = nullptr;

    AppState m_appState = StateListening;

    Handle m_activeHandle = Handle::None;
    bool m_isDraggingWindow = false;
    QPoint m_dragStartPos;
    QRect m_dragStartHoleRect;
    QRect m_dragStartWinGeometry;
    QPoint m_windowDragStartPos;

    const int m_handleSize = 12;
    const int m_borderResizeWidth = 6;
};