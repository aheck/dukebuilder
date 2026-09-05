#pragma once

#include <QPointF>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

class MapDocument
{
public:
    using VertexId = std::size_t;
    using WallId = std::size_t;
    using SpriteId = std::size_t;
    using SectorId = std::size_t;

    struct Vertex {
        QPointF position;
    };

    struct WallSide {
        int texture = 0;
        int overlayTexture = 0;
        int shade = 0;
        int palette = 0;
        int xrepeat = 8;
        int yrepeat = 8;
        int xpanning = 0;
        int ypanning = 0;
        int cstat = 0;
        int hitag = 0;
        int lotag = 0;
        int extra = -1;
    };

    struct Wall {
        VertexId start;
        VertexId end;
        WallSide forwardSide{};
        WallSide reverseSide{};
        // Each side belongs to the sector traversing this edge in that direction.
        std::optional<SectorId> forwardSector = std::nullopt;
        std::optional<SectorId> reverseSector = std::nullopt;

        [[nodiscard]] bool isTwoSided() const
        {
            return forwardSector && reverseSector && forwardSector != reverseSector;
        }
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
        int floorstat = 0;
        int ceilingstat = 0;
        int floorheinum = 0;
        int ceilingheinum = 0;
        int floorshade = 0;
        int ceilingshade = 0;
        int floorpal = 0;
        int ceilingpal = 0;
        int floorxpanning = 0;
        int floorypanning = 0;
        int ceilingxpanning = 0;
        int ceilingypanning = 0;
        int visibility = 0;
        int extra = -1;
    };

    struct Sprite {
        QPointF position;
        qreal z = 0.0;
        qreal angle = 0.0;
        int texture = -1;
        int hitag = 0;
        int lotag = 0;
        int cstat = 0;
        int shade = 0;
        int palette = 0;
        int clipdist = 32;
        int xrepeat = 64;
        int yrepeat = 64;
        int xoffset = 0;
        int yoffset = 0;
        int statnum = 0;
        int owner = -1;
        int xvel = 0;
        int yvel = 0;
        int zvel = 0;
        int extra = -1;
    };

    struct PlayerStart {
        QPointF position;
        qreal z = 0.0;
        qreal angle = 0.0;
    };

    void setWallSide(WallId wallId, bool reversed, const WallSide &side);
    void setSector(SectorId sectorId, const Sector &sector);
    void setSprite(SpriteId spriteId, const Sprite &sprite);
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
