#ifndef CAPTUREOVERLAY_H
#define CAPTUREOVERLAY_H

#include <QWidget>
#include <QTimer>
#include <QPainter>

class CaptureOverlay : public QWidget {
Q_OBJECT
public:
    explicit CaptureOverlay(QWidget *parent = nullptr) : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool) {
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);

        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            m_scanPos += 0.05;
            if (m_scanPos > 1.0) m_scanPos = 0.0;
            update();
        });
    }

    void startScan(const QRect &globalGeometry) {
        setGeometry(globalGeometry);
        m_scanPos = 0.0;
        show();
        m_timer->start(16); // ~60 FPS
    }

    void stopScan() {
        m_timer->stop();
        hide();
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // Semi-transparent blue tint
        painter.fillRect(rect(), QColor(0, 122, 255, 30));

        // Outer border highlight
        QPen borderPen(QColor(0, 122, 255, 200), 2);
        painter.setPen(borderPen);
        painter.drawRect(rect().adjusted(1, 1, -1, -1));

        // Moving animated scanline
        int lineY = static_cast<int>(height() * m_scanPos);
        QLinearGradient grad(0, lineY - 15, 0, lineY + 15);
        grad.setColorAt(0.0, QColor(0, 210, 255, 0));
        grad.setColorAt(0.5, QColor(0, 210, 255, 220));
        grad.setColorAt(1.0, QColor(0, 210, 255, 0));

        painter.fillRect(0, lineY - 15, width(), 30, grad);
    }

private:
    QTimer *m_timer;
    double m_scanPos = 0.0;
};

#endif // CAPTUREOVERLAY_H