#include <QScopeGuard>
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
#include <QPainter>
#include <QWheelEvent>
#include <QResizeEvent>
#include <algorithm>
#include <cmath>

namespace {
class Crosshair final : public QWidget
{
public:
    explicit Crosshair(QWidget *parent) : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setFixedSize(21, 21);
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        for (int width : {3, 1}) {
            painter.setPen(QPen(width == 3 ? Qt::black : Qt::white, width));
            painter.drawLine(3, 10, 17, 10);
            painter.drawLine(10, 3, 10, 17);
        }
    }
};
}

MapView3D::MapView3D(QWidget *parent) : QOpenGLWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    m_crosshair = new Crosshair(this);
    m_crosshair->hide();
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
            if (m_renderer) { m_archive = filename; return true; }
        }
        diagnostic = "Unable to load a renderable game archive: " + diagnostic;
        return false;
    });
    doneCurrent();
    if (!ok) { return false; }
    m_snapshot = std::move(snapshot);
    m_hover = true;
    m_wheelRemainder = 0;
    m_active = true;
    duke_renderer_set_hover_enabled(m_renderer, m_hover);
    duke_renderer_set_sprite_picking_enabled(m_renderer, true);
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
    m_crosshair->move(rect().center() - QPoint(10, 10));
    m_crosshair->show();
    m_crosshair->raise();
    grabMouse(Qt::BlankCursor);
    QCursor::setPos(mapToGlobal(rect().center()));
}
void MapView3D::releaseLook()
{
    m_keys.clear();
    m_crosshair->hide();
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
    if (event->modifiers() == Qt::ControlModifier
        && (event->key() == Qt::Key_C || event->key() == Qt::Key_V)) {
        if (!event->isAutoRepeat()) { editTexture(event->key(), false); }
    } else if (event->key() == Qt::Key_Q && !event->isAutoRepeat()) {
        if (leave3D) { leave3D(); }
    } else if (event->key() == Qt::Key_O) {
        if (!event->isAutoRepeat()) { stickSpriteToWall(); }
    } else if (event->key() == Qt::Key_R) {
        resetTextureScale();
    } else if (event->key() == Qt::Key_Escape) {
        releaseLook();
    } else if (event->key() == Qt::Key_H && !event->isAutoRepeat()) {
        m_hover = !m_hover;
        duke_renderer_set_hover_enabled(m_renderer, m_hover);
        duke_renderer_set_sprite_picking_enabled(m_renderer, true);
    } else if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right
               || event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        editTexture(event->key(), event->modifiers().testFlag(Qt::ShiftModifier));
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
    if (event->button() == Qt::RightButton) { editTexture(0, false); event->accept(); }
}
void MapView3D::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_captured) { return; }
    QPointF delta = event->position() - QPointF(rect().center());
    if (delta.isNull()) { return; }
    duke_camera_rotate(&m_camera, delta.x()*0.003f, -delta.y()*0.003f);
    QCursor::setPos(mapToGlobal(rect().center()));
}
void MapView3D::resizeEvent(QResizeEvent *event)
{
    QOpenGLWidget::resizeEvent(event);
    m_crosshair->move(rect().center() - QPoint(10, 10));
}
void MapView3D::wheelEvent(QWheelEvent *event)
{
    event->accept();
    if (!m_active || !m_renderer || !m_hover) { m_wheelRemainder = 0; return; }
    // Pick using the current camera and pointer rather than a previous frame.
    repaint();
    DukeSurfaceHit hit{};
    if (!duke_renderer_get_hovered_surface(m_renderer, &hit)
        || (hit.kind != DUKE_SURFACE_FLOOR && hit.kind != DUKE_SURFACE_CEILING
            && hit.kind != DUKE_SURFACE_SPRITE)) {
        m_wheelRemainder = 0;
        return;
    }
    // Keep partial notches separate for height/slope and coarse/fine edits.
    const bool fine = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool slopeEdit = hit.kind != DUKE_SURFACE_SPRITE
        && event->modifiers().testFlag(Qt::AltModifier);
    const int mode = (slopeEdit ? 2 : 0) + (fine ? 1 : 0);
    if (continuousEditChanged) continuousEditChanged(QString("wheel:%1:%2:%3:%4")
        .arg(int(hit.kind)).arg(hit.sector_index).arg(hit.sprite_index).arg(mode));
    const auto clearEdit = qScopeGuard([this] {
        if (continuousEditChanged) continuousEditChanged({});
    });
    const qreal heightStep = fine ? 128.0 : 1024.0;
    if (mode != m_wheelMode || hit.kind != m_wheelTarget.kind
        || hit.sector_index != m_wheelTarget.sector_index
        || hit.sprite_index != m_wheelTarget.sprite_index) {
        m_wheelRemainder = 0;
    }
    m_wheelMode = mode;
    m_wheelTarget = hit;
    // Qt's X11 backend transposes wheel axes while Alt is held. Accept that
    // representation as well as vertical deltas from other platforms.
    const QPoint delta = event->angleDelta();
    const int wheelDelta = delta.y() != 0 ? delta.y()
        : event->modifiers().testFlag(Qt::AltModifier) ? delta.x() : 0;
    m_wheelRemainder += wheelDelta;
    const int steps = m_wheelRemainder / 120;
    m_wheelRemainder %= 120;
    if (hit.kind == DUKE_SURFACE_SPRITE) {
        if (!steps || hit.sprite_index < 0
            || std::size_t(hit.sprite_index) >= m_snapshot.sprites().size()) { return; }
        auto sprite = m_snapshot.sprites()[hit.sprite_index];
        // Build Z increases downward. Sprite modifiers do not edit sector slopes.
        sprite.z -= steps * heightStep;
        auto candidate = m_snapshot;
        candidate.setSprite(hit.sprite_index, sprite);
        if (!applySnapshot(std::move(candidate))) { return; }
        if (spriteChanged) { spriteChanged(hit.sprite_index, sprite); }
        if (statusMessage) {
            statusMessage(QString("Sprite %1 Z: %2").arg(hit.sprite_index).arg(sprite.z));
        }
        update();
        return;
    }
    if (!steps || hit.sector_index < 0
        || std::size_t(hit.sector_index) >= m_snapshot.sectors().size()) { return; }
    const bool floor = hit.kind == DUKE_SURFACE_FLOOR;
    const auto &sector = m_snapshot.sectors()[hit.sector_index];
    if (slopeEdit) {
        auto changed = sector;
        int &slope = floor ? changed.floorheinum : changed.ceilingheinum;
        int &flags = floor ? changed.floorstat : changed.ceilingstat;
        slope = std::clamp(slope + steps * (fine ? 16 : 256), -32768, 32767);
        if (slope != 0) { flags |= 2; }
        else { flags &= ~2; }
        if (changed == sector) { return; }
        auto candidate = m_snapshot;
        candidate.setSector(hit.sector_index, changed);
        if (!applySnapshot(std::move(candidate))) { return; }
        if (sectorChanged) { sectorChanged(hit.sector_index, changed); }
        if (statusMessage) {
            statusMessage(QString("Sector %1 %2 slope: %3").arg(hit.sector_index)
                          .arg(floor ? "floor" : "ceiling").arg(slope));
        }
        return;
    }
    // Build Z increases downward: wheel-up raises either surface.
    const qreal height = (floor ? sector.floorz : sector.ceilingz) - steps * heightStep;
    auto candidate = m_snapshot;
    if (floor) { candidate.setSectorFloorZ(hit.sector_index, height); }
    else { candidate.setSectorCeilingZ(hit.sector_index, height); }

    if (!applySnapshot(std::move(candidate))) { return; }
    if (surfaceHeightChanged) { surfaceHeightChanged(hit.sector_index, floor, height); }
    if (statusMessage) {
        statusMessage(QString("Sector %1 %2 Z: %3").arg(hit.sector_index)
                      .arg(floor ? "floor" : "ceiling").arg(height));
    }
    update();
}

bool MapView3D::refreshDocument(const MapDocument &document)
{
    if (!m_active) return true;
    m_wheelRemainder = 0;
    m_keys.clear();
    return applySnapshot(document);
}

bool MapView3D::applySnapshot(MapDocument candidate)
{
    // Validate and upload before committing either the view or editor document.
    // The renderer owns immutable snapshots, so retain the old one on failure.
    QString error;
    DukeRenderer *replacement = nullptr;
    // The snapshot's start is only a validation placeholder, not the live
    // camera or the document's player start. Keep it inside the edited sector
    // when a floor/ceiling moves past its previous Z.
    if (!placePreviewCamera(candidate, candidate.playerStart().position, error)) {
        if (statusMessage) { statusMessage("Cannot edit surface: " + error); }
        return false;
    }
    makeCurrent();
    const bool ok = withBuildMap(candidate, error, [&](DukeMapFile &map, QString &diagnostic) {
        DukeGrpFile *grp = duke_grp_new();
        if (!grp) { diagnostic = "Unable to allocate the GRP archive."; return false; }
        const auto name = QFile::encodeName(m_archive);
        if (duke_grp_open_filename(grp, name.constData()) && duke_grp_read_entries_sparse(grp)) {
            DukeRendererDesc desc{SG_PIXELFORMAT_RGBA8, SG_PIXELFORMAT_DEPTH_STENCIL, 1};
            char message[512]{};
            replacement = duke_renderer_create(&map, grp, &desc, message, sizeof(message));
            diagnostic = QString::fromUtf8(message);
        } else { diagnostic = QString::fromUtf8(grp->last_error); }
        duke_grp_free(grp);
        return replacement != nullptr;
    });
    if (ok) {
        duke_renderer_destroy(m_renderer);
        m_renderer = replacement;
        duke_renderer_set_hover_enabled(m_renderer, m_hover);
        duke_renderer_set_sprite_picking_enabled(m_renderer, true);
        m_snapshot = std::move(candidate);
    }
    doneCurrent();
    m_clock.restart(); // Upload time must not become a navigation step.
    if (!ok) {
        if (statusMessage) { statusMessage("Cannot edit surface: " + error); }
        return false;
    }
    update();
    return true;
}

void MapView3D::editTexture(int key, bool scale)
{
    if (!m_active || !m_renderer || !m_hover) { return; }
    if (key == Qt::Key_V && !m_copiedTexture) { return; }
    repaint();
    DukeSurfaceHit hit{};
    if (!duke_renderer_get_hovered_surface(m_renderer, &hit) || hit.sector_index < 0
        || std::size_t(hit.sector_index) >= m_snapshot.sectors().size()) { return; }
    const bool arrow = key == Qt::Key_Left || key == Qt::Key_Right
        || key == Qt::Key_Up || key == Qt::Key_Down;
    if (continuousEditChanged) continuousEditChanged(arrow
        ? QString("texture:%1:%2:%3:%4:%5:%6").arg(int(hit.kind)).arg(hit.sector_index)
            .arg(hit.wall_index).arg(hit.sprite_index).arg(key).arg(scale) : QString{});
    const auto clearEdit = qScopeGuard([this] {
        if (continuousEditChanged) continuousEditChanged({});
    });
    auto candidate = m_snapshot;
    auto sector = candidate.sectors()[hit.sector_index];
    const bool horizontal = key == Qt::Key_Left || key == Qt::Key_Right;
    const int pan = (key == Qt::Key_Left || key == Qt::Key_Down) ? 1 : -1;
    const bool enlarge = key == Qt::Key_Right || key == Qt::Key_Up;
    const auto wrap = [](int value) { return (value + 256) % 256; };
    // Keep the picked surface fixed while the modal chooser owns input.
    const auto selectTile = [&](int current) -> std::optional<int> {
        if (!chooseTexture) { return std::nullopt; }
        const bool captured = m_captured;
        m_timer.stop();
        releaseLook();
        const auto selection = chooseTexture(current);
        if (m_active) {
            setFocus();
            if (captured) { captureLook(); }
            m_clock.restart();
            m_timer.start();
        }
        return selection;
    };
    if (hit.kind == DUKE_SURFACE_WALL) {
        // Export emits each sector's wall sides consecutively in boundary order.
        int local = hit.wall_index;
        for (int i = 0; i < hit.sector_index; ++i) {
            local -= int(candidate.sectors()[i].walls.size());
        }
        if (local < 0 || std::size_t(local) >= sector.walls.size()) { return; }
        const auto wallId = sector.walls[local];
        const auto &wall = candidate.walls()[wallId];
        const bool reversed = wall.start != sector.vertices[local];
        auto side = reversed ? wall.reverseSide : wall.forwardSide;
        if (key == Qt::Key_R) {
            side.xrepeat = candidate.defaultWallXRepeat(wallId);
            side.yrepeat = 8;
        } else if (key == Qt::Key_C) {
            m_copiedTexture = side.texture;
            if (statusMessage) { statusMessage(QString("Copied texture %1").arg(*m_copiedTexture)); }
            return;
        } else if (key == Qt::Key_V) {
            side.texture = *m_copiedTexture;
        } else if (key == 0) {
            const auto selection = selectTile(side.texture);
            if (!selection || !m_active) { return; }
            side.texture = *selection;
        } else if (scale) {
            int &repeat = horizontal ? side.xrepeat : side.yrepeat;
            // Fewer repeats make each texture copy larger; never collapse to zero.
            repeat = std::clamp(repeat + (enlarge ? -1 : 1), 1, 255);
        } else {
            int &offset = horizontal ? side.xpanning : side.ypanning;
            offset = wrap(offset + pan);
        }
        if (side == (reversed ? wall.reverseSide : wall.forwardSide)) { return; }
        candidate.setWallSide(wallId, reversed, side);
        if (!applySnapshot(std::move(candidate))) { return; }
        if (wallSideChanged) { wallSideChanged(wallId, reversed, side); }
    } else if (hit.kind == DUKE_SURFACE_FLOOR || hit.kind == DUKE_SURFACE_CEILING) {
        if (key == Qt::Key_R) { return; }
        const bool floor = hit.kind == DUKE_SURFACE_FLOOR;
        if (key == Qt::Key_C) {
            m_copiedTexture = floor ? sector.floorTexture : sector.ceilingTexture;
            if (statusMessage) { statusMessage(QString("Copied texture %1").arg(*m_copiedTexture)); }
            return;
        } else if (key == Qt::Key_V) {
            (floor ? sector.floorTexture : sector.ceilingTexture) = *m_copiedTexture;
        } else if (key == 0) {
            int &tile = floor ? sector.floorTexture : sector.ceilingTexture;
            const auto selection = selectTile(tile);
            if (!selection || !m_active) { return; }
            tile = *selection;
        } else if (scale) {
            // Build's double-smoosh flag is the only floor/ceiling scale field.
            int &flags = floor ? sector.floorstat : sector.ceilingstat;
            if (enlarge) { flags &= ~8; } else { flags |= 8; }
        } else {
            int &offset = floor
                ? (horizontal ? sector.floorxpanning : sector.floorypanning)
                : (horizontal ? sector.ceilingxpanning : sector.ceilingypanning);
            offset = wrap(offset + pan);
        }
        if (sector == candidate.sectors()[hit.sector_index]) { return; }
        candidate.setSector(hit.sector_index, sector);
        if (!applySnapshot(std::move(candidate))) { return; }
        if (sectorChanged) { sectorChanged(hit.sector_index, sector); }
    } else { return; }
    if (statusMessage) {
        statusMessage(key == Qt::Key_R ? "Texture scale reset" : (key == 0 || key == Qt::Key_V) ? "Texture changed" : scale ? "Texture size changed" : "Texture offset changed");
    }
}

void MapView3D::stickSpriteToWall()
{
    if (!m_active || !m_renderer || !m_hover) { return; }
    repaint();
    DukeSurfaceHit hit{};
    if (!duke_renderer_get_hovered_surface(m_renderer, &hit) || hit.kind != DUKE_SURFACE_SPRITE
        || hit.sprite_index < 0 || std::size_t(hit.sprite_index) >= m_snapshot.sprites().size()) {
        if (statusMessage) { statusMessage("Point at a sprite to stick it to the nearest wall."); }
        return;
    }
    auto candidate = m_snapshot;
    QString error;
    if (!candidate.stickSpriteToWall(hit.sprite_index, error)) {
        if (statusMessage) { statusMessage(error); }
        return;
    }
    const auto sprite = candidate.sprites()[hit.sprite_index];
    if (!applySnapshot(std::move(candidate))) { return; }
    if (spriteChanged) { spriteChanged(hit.sprite_index, sprite); }
    if (statusMessage) { statusMessage("Sprite placed against the nearest wall."); }
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
