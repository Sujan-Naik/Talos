#pragma once

#include <QAbstractTextDocumentLayout>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextLayout>
#include <QPainter>
#include <QRectF>
#include <QSizeF>

class HoleAwareTextLayout : public QAbstractTextDocumentLayout {
Q_OBJECT
public:
    explicit HoleAwareTextLayout(QTextDocument *doc, const QRect &hole = QRect());

    void setHoleRect(const QRect &hole);
    void doLayout();

    // --- QAbstractTextDocumentLayout Pure Virtual Overrides ---
    QSizeF documentSize() const override;
    void draw(QPainter *painter, const PaintContext &context) override;
    int hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const override;
    int pageCount() const override;
    QRectF frameBoundingRect(QTextFrame *frame) const override;
    QRectF blockBoundingRect(const QTextBlock &block) const override;

protected:
    // Pure virtual method required by QAbstractTextDocumentLayout
    void documentChanged(int position, int charsRemoved, int charsAdded) override;

private:
    QRect m_hole;
    qreal m_totalHeight = 0;
};