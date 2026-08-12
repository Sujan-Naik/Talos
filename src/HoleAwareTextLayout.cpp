#include "../include/HoleAwareTextLayout.h"
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <algorithm>
#include <vector>

struct MessageGroup {
    int userState;
    std::vector<QTextBlock> blocks;
};

static std::vector<MessageGroup> groupDocumentBlocks(QTextDocument *doc) {
    std::vector<MessageGroup> groups;
    QTextBlock block = doc->firstBlock();

    while (block.isValid()) {
        int state = block.userState();
        if (groups.empty() || groups.back().userState != state) {
            groups.push_back({state, {block}});
        } else {
            groups.back().blocks.push_back(block);
        }
        block = block.next();
    }
    return groups;
}

HoleAwareTextLayout::HoleAwareTextLayout(QTextDocument *doc, const QRect &hole)
    : QAbstractTextDocumentLayout(doc), m_hole(hole) {}

void HoleAwareTextLayout::setHoleRect(const QRect &hole) {
    if (m_hole == hole) return;
    m_hole = hole;
    doLayout();
}

void HoleAwareTextLayout::doLayout() {
    qreal currentGlobalY = 15.0;
    const qreal docWidth = document()->pageSize().width();
    const qreal sideMargin = 15.0;
    const qreal clearance = 10.0;

    std::vector<MessageGroup> groups = groupDocumentBlocks(document());

    for (const auto &group : groups) {
        qreal groupHeight = 0;

        for (QTextBlock block : group.blocks) {
            QTextLayout *layout = block.layout();
            if (!layout) continue;

            QTextOption option = document()->defaultTextOption();
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout->setTextOption(option);

            layout->clearLayout();
            layout->beginLayout();

            qreal localBlockY = 0;

            while (true) {
                QTextLine line = layout->createLine();
                if (!line.isValid()) break;

                qreal lineGlobalY = currentGlobalY + localBlockY;
                qreal lineX = sideMargin;
                qreal maxLineWidth = docWidth - (sideMargin * 2.0);
                qreal lineGlobalBottom = lineGlobalY + 20.0;

                bool intersectsHole = m_hole.isValid() &&
                                      (lineGlobalBottom >= m_hole.top() - clearance) &&
                                      (lineGlobalY <= m_hole.bottom() + clearance);

                if (intersectsHole) {
                    qreal leftSpace = (m_hole.left() - clearance) - sideMargin;
                    qreal rightSpace = (docWidth - sideMargin) - (m_hole.right() + clearance);

                    if (rightSpace >= 100.0 && (leftSpace < 100.0 || m_hole.left() <= sideMargin + 50)) {
                        lineX = m_hole.right() + clearance;
                        maxLineWidth = rightSpace;
                    } else if (leftSpace >= 100.0) {
                        lineX = sideMargin;
                        maxLineWidth = leftSpace;
                    } else {
                        qreal dropY = (m_hole.bottom() + clearance) - lineGlobalY;
                        if (dropY > 0) {
                            localBlockY += dropY;
                        }
                    }
                }

                line.setLineWidth(maxLineWidth);
                line.setPosition(QPointF(lineX, localBlockY));
                localBlockY += line.height();
            }
            layout->endLayout();
            groupHeight += localBlockY;
        }

        currentGlobalY += groupHeight + 24.0;
    }

    m_totalHeight = currentGlobalY;
    emit documentSizeChanged(documentSize());
    emit update(QRectF(0, 0, docWidth, m_totalHeight));
}

void HoleAwareTextLayout::documentChanged(int position, int charsRemoved, int charsAdded) {
    Q_UNUSED(position);
    Q_UNUSED(charsRemoved);
    Q_UNUSED(charsAdded);
    doLayout();
}

QSizeF HoleAwareTextLayout::documentSize() const {
    return QSizeF(document()->pageSize().width(), m_totalHeight);
}

void HoleAwareTextLayout::draw(QPainter *painter, const PaintContext &context) {
    Q_UNUSED(context);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    const qreal padX = 10.0;
    const qreal padY = 6.0;
    qreal currentGlobalY = 15.0;

    std::vector<MessageGroup> groups = groupDocumentBlocks(document());

    for (const auto &group : groups) {
        bool isUser = (group.userState == 1);
        QColor bubbleColor = isUser ? QColor(0, 122, 255, 230) : QColor(50, 50, 50, 230);

        qreal groupHeight = 0;
        QPainterPath groupBubblePath;

        for (QTextBlock block : group.blocks) {
            QTextLayout *layout = block.layout();
            if (!layout || layout->lineCount() == 0) continue;

            for (int i = 0; i < layout->lineCount(); ++i) {
                QTextLine line = layout->lineAt(i);
                if (line.naturalTextWidth() <= 0) continue;

                qreal lineX = line.x();
                qreal lineY = currentGlobalY + groupHeight + line.y();

                QRectF lineRect(lineX - padX, lineY - padY,
                                line.naturalTextWidth() + (padX * 2.0),
                                line.height() + (padY * 2.0));

                QPainterPath linePath;
                linePath.addRoundedRect(lineRect, 8, 8);
                groupBubblePath = groupBubblePath.united(linePath);
            }

            groupHeight += layout->boundingRect().height();
        }

        if (!groupBubblePath.isEmpty()) {
            painter->setBrush(bubbleColor);
            painter->setPen(Qt::NoPen);
            painter->drawPath(groupBubblePath);
        }

        qreal blockOffsetY = 0;
        painter->setPen(Qt::white);

        for (QTextBlock block : group.blocks) {
            QTextLayout *layout = block.layout();
            if (!layout) continue;

            for (int i = 0; i < layout->lineCount(); ++i) {
                QTextLine line = layout->lineAt(i);
                QPointF drawPos(line.x(), currentGlobalY + blockOffsetY + line.y());
                line.draw(painter, drawPos);
            }

            blockOffsetY += layout->boundingRect().height();
        }

        currentGlobalY += groupHeight + 24.0;
    }

    painter->restore();
}

int HoleAwareTextLayout::hitTest(const QPointF &point, Qt::HitTestAccuracy accuracy) const {
    Q_UNUSED(point);
    Q_UNUSED(accuracy);
    return -1;
}

int HoleAwareTextLayout::pageCount() const {
    return 1;
}

QRectF HoleAwareTextLayout::frameBoundingRect(QTextFrame *frame) const {
    Q_UNUSED(frame);
    return QRectF(QPointF(0, 0), documentSize());
}

QRectF HoleAwareTextLayout::blockBoundingRect(const QTextBlock &block) const {
    if (!block.isValid()) return QRectF();

    QTextLayout *layout = block.layout();
    if (!layout) return QRectF();

    return QRectF(0, layout->position().y(), document()->pageSize().width(), layout->boundingRect().height());
}