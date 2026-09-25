#pragma once
#include "mapdocument.h"
#include <QOpenGLWidget>
#include <QElapsedTimer>
#include <QTimer>
#include <QSet>
#include <functional>
#include <libduke/camera.h>
#include <libduke/renderer.h>

class MapView3D final : public QOpenGLWidget
{
public:
    explicit MapView3D(QWidget *parent = nullptr);
    ~MapView3D() override;
    bool start(const MapDocument &document, const QPointF &pointer, QString &error);
    void stop();
    void releaseMouseLook() { releaseLook(); }
    void runModal(const std::function<void()> &show);
    bool refreshDocument(const MapDocument &document);
    std::function<void(const QString &)> continuousEditChanged;
    void stickSpriteToWall();
    void resetTextureScale() { editTexture(Qt::Key_R, false); }
    std::function<void()> leave3D;
    std::function<std::optional<int>(int)> chooseTexture;
    std::function<void(std::size_t, bool, qreal)> surfaceHeightChanged;
    std::function<void(std::size_t, const MapDocument::Sector &)> sectorChanged;
    std::function<void(std::size_t, const MapDocument::Sprite &)> spriteChanged;
    std::function<void(std::size_t, bool, const MapDocument::WallSide &)> wallSideChanged;
    std::function<void(const MapDocument &)> shadesChanged;
    std::function<void(const MapDocument &, const QString &)> surfacesChanged;
    std::function<void(const QString &)> statusMessage;
    std::function<void(const QString &)> surfaceStatusChanged;
protected:
    void initializeGL() override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    friend struct MapView3DTest;
    struct SurfaceSelection {
        DukeSurfaceKind kind;
        std::size_t id;
        bool reversed = false;
        bool operator==(const SurfaceSelection &other) const {
            return kind == other.kind && id == other.id && reversed == other.reversed;
        }
    };
    std::optional<SurfaceSelection> selectionFromHit(const DukeSurfaceHit &hit) const;
    std::optional<DukeSurfaceHit> hitFromSelection(const SurfaceSelection &selection) const;
    std::vector<SurfaceSelection> connectedSurfaceGroup(const SurfaceSelection &seed) const;
    void selectConnectedSurfaces(const SurfaceSelection &seed);
    void syncSelection();
    std::vector<SurfaceSelection> m_selection;
    std::size_t m_selectionRevision = 0;
    void releaseLook();
    void captureLook();
    void cleanup();
    bool applySnapshot(MapDocument candidate);
    void editTexture(int key, bool scale,
                     const std::optional<SurfaceSelection> &forcedTarget = std::nullopt,
                     bool maskedTexture = false);
    void showSurfaceContextMenu(const QPoint &globalPosition);
    void alignSelectedWallTextures();
    QSize alignmentTextureSize(int tile) const;
    void editShade(const DukeSurfaceHit &hit, int steps);
    void editSelectedHeights(int steps, bool fine, bool slope);
    bool commitSurfaceEdit(MapDocument candidate, const QString &label);
    void updateSurfaceStatus();
    QString m_surfaceStatus;
    QTimer m_timer;
    QElapsedTimer m_clock;
    QSet<int> m_keys;
    DukeCamera m_camera{};
    DukeRenderer *m_renderer = nullptr;
    bool m_sokol = false;
    bool m_active = false;
    bool m_captured = false;
    bool m_doubleClickSelectionArmed = false;
    bool m_hover = true;
    QWidget *m_crosshair = nullptr;
    MapDocument m_snapshot;
    QString m_archive;
    std::optional<int> m_copiedTexture;
    int m_wheelRemainder = 0;
    int m_wheelMode = 0;
    DukeSurfaceHit m_wheelTarget{};
};
