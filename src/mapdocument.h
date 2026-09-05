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
        qreal floorz = 0.0;
        qreal ceilingz = -8192.0;
        int floorTexture = 0;
        int ceilingTexture = 0;
        int hitag = 0;
        int lotag = 0;
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
    void setSectorFloorZ(std::size_t sectorId, qreal z);
    void setSectorCeilingZ(std::size_t sectorId, qreal z);
    void setSectorFloorTexture(std::size_t sectorId, int texture);
    void setSectorCeilingTexture(std::size_t sectorId, int texture);
    void setSectorHitag(std::size_t sectorId, int hitag);
    void setSectorLotag(std::size_t sectorId, int lotag);
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
