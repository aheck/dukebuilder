#pragma once

#include <QIconEngine>

enum class ToolbarSymbol { DrawMode, LinesMode, VerticesMode, SectorsMode, SpritesMode, New, Open, Save, Grid, Plain, Floor, Ceiling, Move, AddSprite, Rotate, MirrorHorizontal, MirrorVertical };

// Draw toolbar symbols at the requested size for high-DPI screens.
class ToolbarIconEngine final : public QIconEngine
{
public:
    explicit ToolbarIconEngine(ToolbarSymbol symbol);
    QIconEngine *clone() const override;
    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State state) override;
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override;

private:
    ToolbarSymbol m_symbol;
};

QIcon toolbarIcon(ToolbarSymbol symbol);
