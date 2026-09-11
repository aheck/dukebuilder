#include "mapview3d.h"
#include "mapsave.h"
#include "previewcamera.h"
#include <QCursor>
#include <QFile>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QSettings>
#include <algorithm>
#include <cmath>

MapView3D::MapView3D(QWidget *parent) : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setTextureFormat(GL_RGBA8);
    m_timer.setInterval(16);
    connect(&m_timer, &QTimer::timeout, this, [this] { update(); });
}
MapView3D::~MapView3D()
{
    cleanup();
    if (context()) { disconnect(context(), nullptr, this, nullptr); }
}
void MapView3D::initializeGL()
{
    sg_desc desc{};
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    desc.environment.defaults.sample_count = 1;
    desc.image_pool_size = desc.view_pool_size = 8192;
    sg_setup(&desc);
    m_sokol = sg_isvalid();
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &MapView3D::cleanup,
            Qt::DirectConnection);
}
void MapView3D::cleanup()
{
    stop();
    if (m_sokol) {
        makeCurrent();
        duke_renderer_destroy(m_renderer);
        m_renderer = nullptr;
        sg_shutdown();
        m_sokol = false;
        doneCurrent();
    }
}
bool MapView3D::start(const MapDocument &document, const QPointF &pointer, QString &error)
{
    if (!isValid() || !m_sokol) {
        error = "3D mode requires an OpenGL 4.1 context. The graphics context could not be initialized.";
        return false;
    }
    MapDocument snapshot = document;
    if (!placePreviewCamera(snapshot, pointer, error)) { return false; }
    const auto archives = QSettings().value("gameData/grpFiles").toStringList();
    if (archives.isEmpty()) {
        error = "Add DUKE3D.GRP in Settings → Game Data before entering 3D mode.";
        return false;
    }
    makeCurrent();
    duke_renderer_destroy(m_renderer);
    m_renderer = nullptr;
    bool ok = withBuildMap(snapshot, error, [&](DukeMapFile &map, QString &diagnostic) {
        duke_camera_init_from_map(&m_camera, &map);
        for (const auto &filename : archives) {
            DukeGrpFile *grp = duke_grp_new();
            if (!grp) { diagnostic = "Unable to allocate the GRP archive."; return false; }
            const auto name = QFile::encodeName(filename);
            if (duke_grp_open_filename(grp, name.constData()) && duke_grp_read_entries_sparse(grp)) {
                DukeRendererDesc desc{SG_PIXELFORMAT_RGBA8, SG_PIXELFORMAT_DEPTH_STENCIL, 1};
                char message[512]{};
                m_renderer = duke_renderer_create(&map, grp, &desc, message, sizeof(message));
                diagnostic = QString::fromUtf8(message);
            } else { diagnostic = QString::fromUtf8(grp->last_error); }
            duke_grp_free(grp);
            if (m_renderer) { return true; }
        }
        diagnostic = "Unable to load a renderable game archive: " + diagnostic;
        return false;
    });
    doneCurrent();
    if (!ok) { return false; }
    m_active = true;
    duke_renderer_set_hover_enabled(m_renderer, m_hover);
    m_clock.start();
    m_timer.start();
    setFocus();
    captureLook();
    return true;
}
void MapView3D::stop()
{
    m_active = false;
    m_timer.stop();
    releaseLook();
}
void MapView3D::captureLook()
{
    if (!m_active) { return; }
    m_captured = true;
    grabMouse(Qt::BlankCursor);
    QCursor::setPos(mapToGlobal(rect().center()));
}
void MapView3D::releaseLook()
{
    m_keys.clear();
    if (m_captured) { releaseMouse(); m_captured = false; }
}
void MapView3D::paintGL()
{
    if (!m_active || !m_renderer || width() <= 0 || height() <= 0) { return; }
    float dt = std::min(m_clock.restart() / 1000.0f, 0.1f);
    float forward = m_keys.contains(Qt::Key_W) - m_keys.contains(Qt::Key_S);
    float right = m_keys.contains(Qt::Key_D) - m_keys.contains(Qt::Key_A);
    float length = std::max(1.0f, std::hypot(forward, right));
    float step = (m_keys.contains(Qt::Key_Shift) ? 8.0f : 2.0f) * dt / length;
    duke_camera_move(&m_camera, forward*step, right*step);
    float matrix[16];
    if (!duke_camera_view_projection(&m_camera, float(width())/height(), matrix)) { return; }
    QPoint pointer = mapFromGlobal(QCursor::pos());
    duke_renderer_set_pointer(m_renderer, m_captured ? 0 : 2.0f*pointer.x()/width()-1,
                              m_captured ? 0 : 1-2.0f*pointer.y()/height());
    // Qt owns the FBO and presentation. It may replace the FBO on every resize.
    sg_reset_state_cache();
    sg_pass pass{};
    pass.swapchain.width = qRound(width()*devicePixelRatioF());
    pass.swapchain.height = qRound(height()*devicePixelRatioF());
    pass.swapchain.sample_count = 1;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pass.swapchain.gl.framebuffer = defaultFramebufferObject();
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.04f, 0.05f, 0.07f, 1};
    sg_begin_pass(&pass);
    duke_renderer_draw(m_renderer, matrix);
    sg_end_pass();
    sg_commit();
}
void MapView3D::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Q && !event->isAutoRepeat()) {
        if (leave3D) { leave3D(); }
    } else if (event->key() == Qt::Key_Escape) {
        releaseLook();
    } else if (event->key() == Qt::Key_H && !event->isAutoRepeat()) {
        m_hover = !m_hover;
        duke_renderer_set_hover_enabled(m_renderer, m_hover);
    } else { m_keys.insert(event->key()); }
    event->accept();
}
void MapView3D::keyReleaseEvent(QKeyEvent *event)
{
    if (!event->isAutoRepeat()) { m_keys.remove(event->key()); }
    event->accept();
}
void MapView3D::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) { setFocus(); captureLook(); }
}
void MapView3D::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_captured) { return; }
    QPointF delta = event->position() - QPointF(rect().center());
    if (delta.isNull()) { return; }
    duke_camera_rotate(&m_camera, delta.x()*0.003f, -delta.y()*0.003f);
    QCursor::setPos(mapToGlobal(rect().center()));
}
void MapView3D::focusOutEvent(QFocusEvent *event)
{
    releaseLook();
    QOpenGLWidget::focusOutEvent(event);
}
void MapView3D::hideEvent(QHideEvent *event)
{
    stop();
    QOpenGLWidget::hideEvent(event);
}
