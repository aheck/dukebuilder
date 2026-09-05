#pragma once

#include <QPointF>

#include <cstddef>
#include <utility>
#include <vector>

class MapDocument
{
public:
    using VertexId = std::size_t;
    using WallId = std::size_t;
    using SpriteId = std::size_t;

    struct Vertex {
        QPointF position;
    };

    struct Wall {
        VertexId start;
        VertexId end;
    };

    struct Sector {
        std::vector<WallId> walls;
        std::vector<VertexId> vertices;
    };

    struct Sprite {
        QPointF position;
        qreal z = 0.0;
        qreal angle = 0.0;
        int texture = -1;
        int hitag = 0;
        int lotag = 0;
    };

    struct PlayerStart {
        QPointF position;
        qreal z = 0.0;
        qreal angle = 0.0;
    };

    void clear();
    [[nodiscard]] bool addPolyline(const std::vector<QPointF> &points, bool closed);
    void setVertexPositions(const std::vector<std::pair<VertexId, QPointF>> &positions);
    SpriteId addSprite(const QPointF &position);
    void removeSprites(const std::vector<SpriteId> &spriteIds);
    void setSpritePositions(const std::vector<std::pair<SpriteId, QPointF>> &positions);
    void setSpriteZ(SpriteId spriteId, qreal z);
    void setSpriteAngle(SpriteId spriteId, qreal angle);
    void setSpriteTexture(SpriteId spriteId, int texture);
    void setSpriteHitag(SpriteId spriteId, int hitag);
    void setSpriteLotag(SpriteId spriteId, int lotag);
    void setPlayerStartPosition(const QPointF &position);
    void setPlayerStartZ(qreal z);
    void setPlayerStartAngle(qreal angle);

    [[nodiscard]] const std::vector<Vertex> &vertices() const { return m_vertices; }
    [[nodiscard]] const std::vector<Wall> &walls() const { return m_walls; }
    [[nodiscard]] const std::vector<Sector> &sectors() const { return m_sectors; }
    [[nodiscard]] const std::vector<Sprite> &sprites() const { return m_sprites; }
    [[nodiscard]] const PlayerStart &playerStart() const { return m_playerStart; }

private:
    VertexId findOrAddVertex(const QPointF &position);
    void rebuildSectors();

    std::vector<Vertex> m_vertices;
    std::vector<Wall> m_walls;
    std::vector<Sector> m_sectors;
    std::vector<Sprite> m_sprites;
    PlayerStart m_playerStart{{0.0, 0.0}, 0.0, 0.0};
};
