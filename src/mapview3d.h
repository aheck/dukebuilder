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
    std::function<void()> leave3D;
    std::function<std::optional<int>(int)> chooseTexture;
    std::function<void(std::size_t, bool, qreal)> surfaceHeightChanged;
    std::function<void(std::size_t, const MapDocument::Sector &)> sectorChanged;
    std::function<void(std::size_t, bool, const MapDocument::WallSide &)> wallSideChanged;
    std::function<void(const QString &)> statusMessage;
protected:
    void initializeGL() override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    void releaseLook();
    void captureLook();
    void cleanup();
    bool applySnapshot(MapDocument candidate);
    void editTexture(int key, bool scale);
    QTimer m_timer;
    QElapsedTimer m_clock;
    QSet<int> m_keys;
    DukeCamera m_camera{};
    DukeRenderer *m_renderer = nullptr;
    bool m_sokol = false;
    bool m_active = false;
    bool m_captured = false;
    bool m_hover = true;
    QWidget *m_crosshair = nullptr;
    MapDocument m_snapshot;
    QString m_archive;
    std::optional<int> m_copiedTexture;
    int m_wheelRemainder = 0;
    int m_wheelMode = 0;
    DukeSurfaceHit m_wheelTarget{};
};
