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
protected:
    void initializeGL() override;
    void paintGL() override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hideEvent(QHideEvent *event) override;
private:
    void releaseLook();
    void captureLook();
    void cleanup();
    QTimer m_timer;
    QElapsedTimer m_clock;
    QSet<int> m_keys;
    DukeCamera m_camera{};
    DukeRenderer *m_renderer = nullptr;
    bool m_sokol = false;
    bool m_active = false;
    bool m_captured = false;
    bool m_hover = false;
};
