#include "toolbariconengine.h"
#include <QApplication>
#include <QPainter>
#include <QPalette>
#include <QPixmap>

ToolbarIconEngine::ToolbarIconEngine(ToolbarSymbol symbol) : m_symbol(symbol) {}

QIconEngine *ToolbarIconEngine::clone() const { return new ToolbarIconEngine(m_symbol); }

void ToolbarIconEngine::paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State)
{
    painter->save();
    painter->translate(rect.center().x() - rect.width() / 2.0,
                       rect.center().y() - rect.height() / 2.0);
    painter->scale(rect.width() / 24.0, rect.height() / 24.0);
    painter->setRenderHint(QPainter::Antialiasing);
    const auto palette = QApplication::palette();
    const auto group = mode == QIcon::Disabled ? QPalette::Disabled : QPalette::Active;
    const QColor ink = palette.color(group, QPalette::ButtonText);
    const QColor accent = palette.color(group, QPalette::Highlight);
    painter->setPen(QPen(ink, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (m_symbol == ToolbarSymbol::DrawMode) {
        painter->setBrush(accent);
        painter->drawPolygon(QPolygonF{QPointF(4, 20), QPointF(6, 14),
            QPointF(17, 3), QPointF(21, 7), QPointF(10, 18)});
        painter->drawLine(QPointF(14, 6), QPointF(18, 10));
    } else if (m_symbol == ToolbarSymbol::LinesMode) {
        painter->setPen(QPen(accent, 2.5));
        painter->drawLine(QPointF(5, 19), QPointF(19, 5));
        painter->setBrush(ink);
        painter->drawRect(QRectF(3, 17, 4, 4));
        painter->drawRect(QRectF(17, 3, 4, 4));
    } else if (m_symbol == ToolbarSymbol::VerticesMode) {
        painter->drawPolygon(QPolygonF{QPointF(5, 19), QPointF(8, 4), QPointF(20, 15)});
        painter->setBrush(accent);
        for (const auto point : {QPointF(5, 19), QPointF(8, 4), QPointF(20, 15)}) {
            painter->drawEllipse(point, 2.5, 2.5);
        }
    } else if (m_symbol == ToolbarSymbol::SectorsMode) {
        painter->setBrush(accent);
        painter->drawPolygon(QPolygonF{QPointF(4, 5), QPointF(17, 3),
            QPointF(21, 14), QPointF(14, 21), QPointF(3, 17)});
    } else if (m_symbol == ToolbarSymbol::SpritesMode) {
        painter->drawEllipse(QRectF(4, 4, 16, 16));
        painter->setBrush(accent);
        painter->drawPolygon(QPolygonF{QPointF(9, 15), QPointF(11, 10),
            QPointF(20, 4), QPointF(14, 13)});
    } else if (m_symbol == ToolbarSymbol::New) {
        // Folded document with a plus sign.
        painter->drawPolygon(QPolygonF{QPointF(5, 3), QPointF(14, 3), QPointF(19, 8),
                                      QPointF(19, 21), QPointF(5, 21)});
        painter->drawPolyline(QPolygonF{QPointF(14, 3), QPointF(14, 8), QPointF(19, 8)});
        painter->setPen(QPen(accent, 2, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(8, 14), QPointF(16, 14));
        painter->drawLine(QPointF(12, 10), QPointF(12, 18));
    } else if (m_symbol == ToolbarSymbol::Open) {
        // Open folder with a raised front flap.
        painter->drawPolygon(QPolygonF{QPointF(3, 19), QPointF(3, 5), QPointF(9, 5),
                                      QPointF(11, 8), QPointF(20, 8), QPointF(20, 19)});
        painter->setBrush(accent);
        painter->drawPolygon(QPolygonF{QPointF(3, 19), QPointF(6, 11),
                                      QPointF(22, 11), QPointF(19, 19)});
    } else if (m_symbol == ToolbarSymbol::Save) {
        // Floppy disk with a shutter and label.
        painter->setBrush(accent);
        painter->drawPolygon(QPolygonF{QPointF(4, 3), QPointF(17, 3), QPointF(21, 7),
                                      QPointF(21, 21), QPointF(3, 21), QPointF(3, 4)});
        painter->setBrush(palette.color(group, QPalette::Button));
        painter->drawRect(QRectF(7, 3, 9, 6));
        painter->drawRect(QRectF(7, 13, 10, 8));
        painter->drawLine(QPointF(10, 17), QPointF(14, 17));
    } else if (m_symbol == ToolbarSymbol::Move) {
        painter->drawLine(QPointF(3, 12), QPointF(21, 12));
        painter->drawLine(QPointF(12, 3), QPointF(12, 21));
        for (int angle : {0, 90, 180, 270}) {
            painter->save();
            painter->translate(12, 12);
            painter->rotate(angle);
            painter->drawPolyline(QPolygonF{QPointF(-3, -6), QPointF(0, -9), QPointF(3, -6)});
            painter->restore();
        }
    } else if (m_symbol == ToolbarSymbol::Rotate) {
        painter->drawArc(QRectF(4, 4, 16, 16), 30 * 16, 285 * 16);
        painter->drawPolyline(QPolygonF{QPointF(16, 3), QPointF(21, 5), QPointF(19, 10)});
    } else if (m_symbol == ToolbarSymbol::MirrorHorizontal || m_symbol == ToolbarSymbol::MirrorVertical) {
        if (m_symbol == ToolbarSymbol::MirrorVertical) {
            painter->translate(24, 0); painter->rotate(90);
        }
        painter->drawLine(QPointF(12, 2), QPointF(12, 22));
        painter->drawPolygon(QPolygonF{QPointF(3, 18), QPointF(9, 6), QPointF(9, 18)});
        painter->drawPolygon(QPolygonF{QPointF(21, 18), QPointF(15, 6), QPointF(15, 18)});
    } else if (m_symbol == ToolbarSymbol::AddSprite) {
        painter->drawEllipse(QRectF(3, 3, 12, 12));
        painter->drawLine(QPointF(9, 9), QPointF(13, 5));
        painter->setPen(QPen(accent, 2, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(QPointF(13, 17), QPointF(23, 17));
        painter->drawLine(QPointF(18, 12), QPointF(18, 22));
    } else if (m_symbol == ToolbarSymbol::Grid) {
        for (int coordinate : {4, 12, 20}) {
            painter->drawLine(QPointF(coordinate, 4), QPointF(coordinate, 20));
            painter->drawLine(QPointF(4, coordinate), QPointF(20, coordinate));
        }
    } else if (m_symbol == ToolbarSymbol::Plain) {
        painter->setBrush(accent);
        painter->drawRoundedRect(QRectF(4, 4, 16, 16), 2, 2);
    } else {
        // Mirror the textured plane to distinguish the ceiling from the floor.
        if (m_symbol == ToolbarSymbol::Ceiling) {
            painter->translate(0, 24);
            painter->scale(1, -1);
        }
        painter->drawLine(QPointF(4, 5), QPointF(4, 20));
        painter->drawLine(QPointF(20, 5), QPointF(20, 20));
        painter->setBrush(accent);
        painter->drawPolygon(QPolygonF{QPointF(4, 20), QPointF(20, 20),
                                      QPointF(17, 12), QPointF(7, 12)});
        painter->drawLine(QPointF(12, 12), QPointF(12, 20));
        painter->drawLine(QPointF(5.5, 16), QPointF(18.5, 16));
    }
    painter->restore();
}

QPixmap ToolbarIconEngine::pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state)
{
    QPixmap result(size);
    result.fill(Qt::transparent);
    QPainter painter(&result);
    paint(&painter, QRect(QPoint(), size), mode, state);
    return result;
}

QIcon toolbarIcon(ToolbarSymbol symbol)
{
    return QIcon(new ToolbarIconEngine(symbol));
}
