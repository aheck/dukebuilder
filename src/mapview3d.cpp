#include <QScopeGuard>
#include "mapview3d.h"
#include "mapsave.h"
#include "previewcamera.h"
#include "walltexturealignment.h"
#include <libduke/art.h>
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
#include <memory>

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
    }, BuildMapValidation::Preview);
    doneCurrent();
    if (!ok) { return false; }
    m_snapshot = std::move(snapshot);
    m_selection.clear();
    ++m_selectionRevision;
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
    m_selection.clear();
    ++m_selectionRevision;
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
    updateSurfaceStatus();
}

void MapView3D::updateSurfaceStatus()
{
    const auto describe = [this](const DukeSurfaceHit &hit) -> QString {
        if (hit.kind == DUKE_SURFACE_SPRITE && hit.sprite_index >= 0
            && std::size_t(hit.sprite_index) < m_snapshot.sprites().size())
            return QString("Sprite shade: %1").arg(m_snapshot.sprites()[hit.sprite_index].shade);
        if (hit.sector_index < 0 || std::size_t(hit.sector_index) >= m_snapshot.sectors().size()) return {};
        const auto &sector = m_snapshot.sectors()[hit.sector_index];
        if (hit.kind == DUKE_SURFACE_FLOOR) return QString("Floor shade: %1").arg(sector.floorshade);
        if (hit.kind == DUKE_SURFACE_CEILING) return QString("Ceiling shade: %1").arg(sector.ceilingshade);
        if (hit.kind == DUKE_SURFACE_WALL) {
            int local = hit.wall_index;
            for (int s = 0; s < hit.sector_index; ++s) local -= int(m_snapshot.sectors()[s].walls.size());
            if (local < 0 || std::size_t(local) >= sector.walls.size()) return {};
            const auto &wall = m_snapshot.walls()[sector.walls[local]];
            const auto &side = wall.start == sector.vertices[local] ? wall.forwardSide : wall.reverseSide;
            return QString("Wall shade: %1").arg(side.shade);
        }
        return {};
    };
    DukeSurfaceHit hovered{}, selected{};
    QString text = "Shade: —";
    if (duke_renderer_get_hovered_surface(m_renderer, &hovered)) {
        const auto description = describe(hovered);
        if (!description.isEmpty()) text = description;
    }
    if (m_selection.size() == 1 && hitFromSelection(m_selection.front())) {
        selected = *hitFromSelection(m_selection.front());
        const auto description = describe(selected);
        if (!description.isEmpty()) text = "Selected " + description;
    } else if (m_selection.size() > 1) {
        QString shade;
        for (const auto &entry : m_selection) {
            const auto hit = hitFromSelection(entry);
            if (!hit) continue;
            const auto value = describe(*hit).section(": ", 1);
            if (shade.isEmpty()) shade = value;
            else if (shade != value) { shade = "mixed"; break; }
        }
        text = QString("%1 selected · Shade: %2").arg(m_selection.size()).arg(shade);
    }
    if (text != m_surfaceStatus) {
        m_surfaceStatus = text;
        if (surfaceStatusChanged) surfaceStatusChanged(text);
    }
}
void MapView3D::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == Qt::ControlModifier
        && (event->key() == Qt::Key_C || event->key() == Qt::Key_V)) {
        if (!event->isAutoRepeat()) { editTexture(event->key(), false); }
    } else if (event->key() == Qt::Key_A && event->modifiers() == Qt::NoModifier && m_selection.size() > 1
               && std::any_of(m_selection.begin(), m_selection.end(), [](const auto &entry) {
                   return entry.kind == DUKE_SURFACE_WALL;
               })) {
        m_keys.remove(Qt::Key_A);
        if (!event->isAutoRepeat()) { alignSelectedWallTextures(); }
    } else if (event->key() == Qt::Key_Q && !event->isAutoRepeat()) {
        if (leave3D) { leave3D(); }
    } else if (event->key() == Qt::Key_O) {
        if (!event->isAutoRepeat()) { stickSpriteToWall(); }
    } else if (event->key() == Qt::Key_R) {
        resetTextureScale();
    } else if (event->key() == Qt::Key_Escape) {
        if (!event->isAutoRepeat()) {
            if (!m_selection.empty()) {
                m_selection.clear();
                ++m_selectionRevision;
                syncSelection();
                m_wheelRemainder = 0;
            } else {
                releaseLook();
            }
        }
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
    if (event->button() == Qt::LeftButton) {
        m_doubleClickSelectionArmed = m_active && m_renderer && m_captured;
    }
    if (event->button() == Qt::LeftButton) {
        // The first click after releasing mouse look only resumes 3D controls.
        if (m_active && m_renderer && m_captured) {
            // Resolve the clicked object before capture moves the pointer.
            repaint();
            DukeSurfaceHit hit{};
            duke_renderer_get_hovered_surface(m_renderer, &hit);
            const auto selected = selectionFromHit(hit);
            if (event->modifiers().testFlag(Qt::ShiftModifier)) {
                if (selected) {
                    const auto it = std::find(m_selection.begin(), m_selection.end(), *selected);
                    if (it == m_selection.end()) m_selection.push_back(*selected);
                    else m_selection.erase(it);
                }
            } else {
                // Retain click-again deselection for an existing single selection.
                const bool same = selected && m_selection.size() == 1 && m_selection.front() == *selected;
                m_selection.clear();
                if (selected && !same) m_selection.push_back(*selected);
            }
            ++m_selectionRevision;
            syncSelection();
            m_wheelRemainder = 0;
        }
        setFocus();
        captureLook();
        event->accept();
    }
    if (event->button() == Qt::RightButton) { editTexture(0, false); event->accept(); }
}
void MapView3D::mouseDoubleClickEvent(QMouseEvent *event)
{
    const bool wasCaptured = m_doubleClickSelectionArmed;
    m_doubleClickSelectionArmed = false;
    if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ShiftModifier
        && wasCaptured && m_active && m_renderer) {
        repaint();
        DukeSurfaceHit hit{};
        if (duke_renderer_get_hovered_surface(m_renderer, &hit)) {
            if (const auto seed = selectionFromHit(hit)) {
                selectConnectedSurfaces(*seed);
                event->accept();
                return;
            }
        }
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
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
    if (!m_active || !m_renderer) { m_wheelRemainder = 0; return; }
    // Pick using the current camera and pointer rather than a previous frame.
    repaint();
    DukeSurfaceHit hit{};
    const bool shadeEdit = event->modifiers().testFlag(Qt::ControlModifier);
    const auto selected = m_selection.empty() ? std::optional<DukeSurfaceHit>{} : hitFromSelection(m_selection.front());
    if (selected) hit = *selected;
    if ((!selected
         && !duke_renderer_get_hovered_surface(m_renderer, &hit))
        || (hit.kind != DUKE_SURFACE_FLOOR && hit.kind != DUKE_SURFACE_CEILING
            && hit.kind != DUKE_SURFACE_SPRITE
            && !((shadeEdit || m_selection.size() > 1) && hit.kind == DUKE_SURFACE_WALL))) {
        m_wheelRemainder = 0;
        return;
    }
    // Keep partial notches separate for height, slope, shade and coarse/fine edits.
    const bool fine = event->modifiers().testFlag(Qt::ShiftModifier);
    const bool slopeEdit = (m_selection.size() > 1 || hit.kind != DUKE_SURFACE_SPRITE)
        && event->modifiers().testFlag(Qt::AltModifier);
    const int mode = shadeEdit ? 4 : (slopeEdit ? 2 : 0) + (fine ? 1 : 0);
    if (continuousEditChanged) continuousEditChanged(m_selection.empty()
        ? QString("wheel:%1:%2:%3:%4:%5").arg(int(hit.kind)).arg(hit.sector_index)
            .arg(hit.sprite_index).arg(hit.wall_index).arg(mode)
        : QString("selection-wheel:%1:%2").arg(m_selectionRevision).arg(mode));
    const auto clearEdit = qScopeGuard([this] {
        if (continuousEditChanged) continuousEditChanged({});
    });
    const qreal heightStep = fine ? 128.0 : 1024.0;
    if (mode != m_wheelMode || hit.kind != m_wheelTarget.kind
        || hit.sector_index != m_wheelTarget.sector_index
        || hit.wall_index != m_wheelTarget.wall_index
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
    if (shadeEdit) {
        if (steps) editShade(hit, steps);
        return;
    }
    if (m_selection.size() > 1) {
        if (steps) editSelectedHeights(steps, fine, slopeEdit);
        return;
    }
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

bool MapView3D::commitSurfaceEdit(MapDocument candidate, const QString &label)
{
    if (candidate == m_snapshot || !applySnapshot(std::move(candidate))) return false;
    if (surfacesChanged) surfacesChanged(m_snapshot, label);
    if (statusMessage) statusMessage(label);
    return true;
}

void MapView3D::editSelectedHeights(int steps, bool fine, bool slopeEdit)
{
    auto candidate = m_snapshot;
    const qreal delta = steps * (fine ? 128.0 : 1024.0);
    for (const auto &target : m_selection) {
        if (!hitFromSelection(target)) return;
        if (target.kind == DUKE_SURFACE_WALL) continue;
        if (target.kind == DUKE_SURFACE_SPRITE) {
            if (slopeEdit) continue;
            auto sprite = candidate.sprites()[target.id];
            sprite.z -= delta;
            candidate.setSprite(target.id, sprite);
        } else {
            auto sector = candidate.sectors()[target.id];
            const bool floor = target.kind == DUKE_SURFACE_FLOOR;
            if (slopeEdit) {
                int &slope = floor ? sector.floorheinum : sector.ceilingheinum;
                int &flags = floor ? sector.floorstat : sector.ceilingstat;
                slope = std::clamp(slope + steps * (fine ? 16 : 256), -32768, 32767);
                if (slope) flags |= 2;
                else flags &= ~2;
            } else {
                (floor ? sector.floorz : sector.ceilingz) -= delta;
            }
            candidate.setSector(target.id, sector);
        }
    }
    commitSurfaceEdit(std::move(candidate), slopeEdit ? "Change selected slopes" : "Change selected heights");
}

void MapView3D::editShade(const DukeSurfaceHit &hit, int steps)
{
    auto targets = m_selection;
    if (targets.empty()) {
        const auto hovered = selectionFromHit(hit);
        if (!hovered) return;
        targets.push_back(*hovered);
    }
    auto candidate = m_snapshot;
    const auto adjusted = [steps](int shade) { return std::clamp(shade + steps, -128, 127); };
    for (const auto &target : targets) {
        if (!hitFromSelection(target)) return;
        if (target.kind == DUKE_SURFACE_SPRITE) {
            auto sprite = candidate.sprites()[target.id];
            sprite.shade = adjusted(sprite.shade);
            candidate.setSprite(target.id, sprite);
        } else if (target.kind == DUKE_SURFACE_WALL) {
            const auto &wall = candidate.walls()[target.id];
            auto side = target.reversed ? wall.reverseSide : wall.forwardSide;
            side.shade = adjusted(side.shade);
            candidate.setWallSide(target.id, target.reversed, side);
        } else {
            auto sector = candidate.sectors()[target.id];
            int &shade = target.kind == DUKE_SURFACE_FLOOR ? sector.floorshade : sector.ceilingshade;
            shade = adjusted(shade);
            candidate.setSector(target.id, sector);
        }
    }
    if (candidate == m_snapshot || !applySnapshot(std::move(candidate))) return;
    if (shadesChanged) shadesChanged(m_snapshot);
    updateSurfaceStatus();
}

std::optional<MapView3D::SurfaceSelection> MapView3D::selectionFromHit(const DukeSurfaceHit &hit) const
{
    if (hit.kind == DUKE_SURFACE_SPRITE) {
        if (hit.sprite_index < 0 || std::size_t(hit.sprite_index) >= m_snapshot.sprites().size()) return {};
        return SurfaceSelection{hit.kind, std::size_t(hit.sprite_index)};
    }
    if (hit.sector_index < 0 || std::size_t(hit.sector_index) >= m_snapshot.sectors().size()) return {};
    if (hit.kind == DUKE_SURFACE_FLOOR || hit.kind == DUKE_SURFACE_CEILING)
        return SurfaceSelection{hit.kind, std::size_t(hit.sector_index)};
    if (hit.kind != DUKE_SURFACE_WALL) return {};
    int local = hit.wall_index;
    for (int s = 0; s < hit.sector_index; ++s) local -= int(m_snapshot.sectors()[s].walls.size());
    const auto &sector = m_snapshot.sectors()[hit.sector_index];
    if (local < 0 || std::size_t(local) >= sector.walls.size()) return {};
    const auto id = sector.walls[local];
    return SurfaceSelection{hit.kind, id, m_snapshot.walls()[id].start != sector.vertices[local]};
}

std::optional<DukeSurfaceHit> MapView3D::hitFromSelection(const SurfaceSelection &selection) const
{
    DukeSurfaceHit hit{};
    hit.kind = selection.kind;
    hit.sector_index = hit.wall_index = hit.sprite_index = -1;
    if (selection.kind == DUKE_SURFACE_SPRITE) {
        if (selection.id >= m_snapshot.sprites().size()) return {};
        hit.sprite_index = int(selection.id);
        const auto sector = m_snapshot.sprites()[selection.id].sectorId;
        if (sector) hit.sector_index = int(*sector);
    } else if (selection.kind == DUKE_SURFACE_WALL) {
        if (selection.id >= m_snapshot.walls().size()) return {};
        const auto &wall = m_snapshot.walls()[selection.id];
        const auto owner = selection.reversed ? wall.reverseSector : wall.forwardSector;
        if (!owner || *owner >= m_snapshot.sectors().size()) return {};
        hit.sector_index = int(*owner);
        const auto &walls = m_snapshot.sectors()[*owner].walls;
        const auto it = std::find(walls.begin(), walls.end(), selection.id);
        if (it == walls.end()) return {};
        hit.wall_index = int(it - walls.begin());
        for (std::size_t s = 0; s < *owner; ++s) hit.wall_index += int(m_snapshot.sectors()[s].walls.size());
    } else {
        if (selection.id >= m_snapshot.sectors().size()
            || (selection.kind != DUKE_SURFACE_FLOOR && selection.kind != DUKE_SURFACE_CEILING)) return {};
        hit.sector_index = int(selection.id);
    }
    return hit;
}

std::vector<MapView3D::SurfaceSelection> MapView3D::connectedSurfaceGroup(
    const SurfaceSelection &seed) const
{
    if (seed.kind != DUKE_SURFACE_FLOOR && seed.kind != DUKE_SURFACE_CEILING
        && seed.kind != DUKE_SURFACE_WALL) return {};
    if ((seed.kind == DUKE_SURFACE_WALL && seed.id >= m_snapshot.walls().size())
        || ((seed.kind == DUKE_SURFACE_FLOOR || seed.kind == DUKE_SURFACE_CEILING)
            && seed.id >= m_snapshot.sectors().size())) return {};

    std::vector<SurfaceSelection> group{seed};
    const auto appendUnique = [&](const SurfaceSelection &candidate) {
        if (std::find(group.begin(), group.end(), candidate) == group.end()) {
            group.push_back(candidate);
        }
    };
    const auto surfaceZ = [&](MapDocument::SectorId id, bool floor, const QPointF &point) {
        const auto &sector = m_snapshot.sectors()[id];
        qreal z = floor ? sector.floorz : sector.ceilingz;
        const int flags = floor ? sector.floorstat : sector.ceilingstat;
        const int heinum = floor ? sector.floorheinum : sector.ceilingheinum;
        if ((flags & 2) == 0 || sector.vertices.empty()) return z;
        const QPointF a = m_snapshot.vertices()[sector.vertices[0]].position;
        const QPointF b = m_snapshot.vertices()[sector.vertices[sector.nextWallIndex(0)]].position;
        const QPointF delta = b - a;
        const qreal length = std::hypot(delta.x(), delta.y());
        if (length > 0.0) {
            z += heinum * (delta.x() * (point.y() - a.y())
                           - delta.y() * (point.x() - a.x())) / (length * 256.0);
        }
        return z;
    };
    const auto surfacesMeet = [&](MapDocument::SectorId a, MapDocument::SectorId b,
                                  bool floor, const MapDocument::Wall &wall) {
        const QPointF start = m_snapshot.vertices()[wall.start].position;
        const QPointF end = m_snapshot.vertices()[wall.end].position;
        constexpr qreal heightTolerance = 1.0;
        return std::abs(surfaceZ(a, floor, start) - surfaceZ(b, floor, start)) <= heightTolerance
            && std::abs(surfaceZ(a, floor, end) - surfaceZ(b, floor, end)) <= heightTolerance;
    };

    for (std::size_t cursor = 0; cursor < group.size(); ++cursor) {
        const auto current = group[cursor];
        if (current.kind == DUKE_SURFACE_FLOOR || current.kind == DUKE_SURFACE_CEILING) {
            const bool floor = current.kind == DUKE_SURFACE_FLOOR;
            const auto &sector = m_snapshot.sectors()[current.id];
            for (const auto wallId : sector.walls) {
                if (wallId >= m_snapshot.walls().size()) continue;
                const auto &wall = m_snapshot.walls()[wallId];
                std::optional<MapDocument::SectorId> neighbor;
                if (wall.forwardSector == current.id) neighbor = wall.reverseSector;
                else if (wall.reverseSector == current.id) neighbor = wall.forwardSector;
                if (!neighbor || *neighbor >= m_snapshot.sectors().size()
                    || *neighbor == current.id) continue;
                if (surfacesMeet(current.id, *neighbor, floor, wall)) {
                    appendUnique({current.kind, *neighbor});
                }
            }
            continue;
        }

        const auto &currentWall = m_snapshot.walls()[current.id];
        const auto owner = current.reversed ? currentWall.reverseSector : currentWall.forwardSector;
        if (!owner || *owner >= m_snapshot.sectors().size()) continue;
        const auto &currentSide = current.reversed ? currentWall.reverseSide : currentWall.forwardSide;
        const auto &sector = m_snapshot.sectors()[*owner];
        for (std::size_t local = 0; local < sector.walls.size(); ++local) {
            const auto wallId = sector.walls[local];
            if (wallId >= m_snapshot.walls().size()) continue;
            const auto &wall = m_snapshot.walls()[wallId];
            const bool reversed = wall.start != sector.vertices[local];
            const auto &side = reversed ? wall.reverseSide : wall.forwardSide;
            if (side.texture != currentSide.texture || side.overlayTexture != currentSide.overlayTexture
                || side.palette != currentSide.palette) continue;
            const bool sharesEndpoint = wall.start == currentWall.start || wall.start == currentWall.end
                || wall.end == currentWall.start || wall.end == currentWall.end;
            if (sharesEndpoint) appendUnique({DUKE_SURFACE_WALL, wallId, reversed});
        }
    }
    return group;
}

void MapView3D::selectConnectedSurfaces(const SurfaceSelection &seed)
{
    const auto group = connectedSurfaceGroup(seed);
    if (group.empty()) return;
    std::size_t added = 0;
    for (const auto &entry : group) {
        if (std::find(m_selection.begin(), m_selection.end(), entry) == m_selection.end()) {
            m_selection.push_back(entry);
            ++added;
        }
    }
    if (added) {
        ++m_selectionRevision;
        syncSelection();
        if (statusMessage) {
            const QString surface = seed.kind == DUKE_SURFACE_FLOOR ? "floor"
                : seed.kind == DUKE_SURFACE_CEILING ? "ceiling" : "wall";
            statusMessage(QString("Added %1 connected matching %2 surface(s) to selection")
                          .arg(added).arg(surface));
        }
    }
}

void MapView3D::syncSelection()
{
    if (!m_renderer) return;
    std::vector<DukeSurfaceHit> hits;
    for (const auto &entry : m_selection) {
        const auto hit = hitFromSelection(entry);
        if (hit) hits.push_back(*hit);
    }
    if (hits.size() != m_selection.size()
        || !duke_renderer_set_selected_surfaces(m_renderer, hits.data(), hits.size())) {
        m_selection.clear();
        ++m_selectionRevision;
        duke_renderer_set_selected_surfaces(m_renderer, nullptr, 0);
    }
    updateSurfaceStatus();
    update();
}

void MapView3D::runModal(const std::function<void()> &show)
{
    const bool captured = m_captured;
    const bool running = m_timer.isActive();
    m_timer.stop();
    releaseLook();
    show();
    if (m_active) {
        setFocus();
        if (captured) captureLook();
        m_clock.restart();
        if (running) m_timer.start();
    }
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
    }, BuildMapValidation::Preview);
    if (ok) {
        // Property edits preserve document identities; geometry changes may not.
        bool topologyChanged = candidate.vertices() != m_snapshot.vertices()
            || candidate.sectors().size() != m_snapshot.sectors().size()
            || candidate.walls().size() != m_snapshot.walls().size()
            || candidate.sprites().size() != m_snapshot.sprites().size();
        if (!topologyChanged) {
            for (std::size_t s = 0; s < candidate.sectors().size(); ++s)
                topologyChanged = topologyChanged || candidate.sectors()[s].walls != m_snapshot.sectors()[s].walls
                    || candidate.sectors()[s].vertices != m_snapshot.sectors()[s].vertices;
        }
        if (topologyChanged) { m_selection.clear(); ++m_selectionRevision; }
        duke_renderer_destroy(m_renderer);
        m_renderer = replacement;
        duke_renderer_set_hover_enabled(m_renderer, m_hover);
        duke_renderer_set_sprite_picking_enabled(m_renderer, true);
        m_snapshot = std::move(candidate);
        syncSelection();
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
    if (!m_active || !m_renderer || (!m_hover && m_selection.size() < 2)) return;
    if (key == Qt::Key_V && !m_copiedTexture) return;
    repaint();
    DukeSurfaceHit hit{};
    const bool hasHover = duke_renderer_get_hovered_surface(m_renderer, &hit);
    const auto hovered = hasHover ? selectionFromHit(hit) : std::optional<SurfaceSelection>{};
    // Copy samples the pointed-at texture; edits use the multi-selection.
    // Retain the existing hover behavior for single-selection texture editing.
    auto targets = m_selection.size() > 1 && key != Qt::Key_C
        ? m_selection : std::vector<SurfaceSelection>{};
    if (targets.empty()) {
        if (!hovered) return;
        targets.push_back(*hovered);
    }
    for (const auto &target : targets) {
        if (!hitFromSelection(target)) return;
    }
    const auto texture = [this](const SurfaceSelection &target) {
        if (target.kind == DUKE_SURFACE_SPRITE) return m_snapshot.sprites()[target.id].texture;
        if (target.kind == DUKE_SURFACE_WALL) {
            const auto &wall = m_snapshot.walls()[target.id];
            return (target.reversed ? wall.reverseSide : wall.forwardSide).texture;
        }
        const auto &sector = m_snapshot.sectors()[target.id];
        return target.kind == DUKE_SURFACE_FLOOR ? sector.floorTexture : sector.ceilingTexture;
    };
    if (key == Qt::Key_C) {
        m_copiedTexture = texture(targets.front());
        if (statusMessage) statusMessage(QString("Copied texture %1").arg(*m_copiedTexture));
        return;
    }
    const bool arrow = key == Qt::Key_Left || key == Qt::Key_Right
        || key == Qt::Key_Up || key == Qt::Key_Down;
    if (continuousEditChanged) continuousEditChanged(!arrow ? QString{} : targets.size() > 1
        ? QString("selection-texture:%1:%2:%3").arg(m_selectionRevision).arg(key).arg(scale)
        : QString("texture:%1:%2:%3:%4:%5:%6").arg(int(hit.kind)).arg(hit.sector_index)
            .arg(hit.wall_index).arg(hit.sprite_index).arg(key).arg(scale));
    const auto clearEdit = qScopeGuard([this] {
        if (continuousEditChanged) continuousEditChanged({});
    });
    std::optional<int> tile;
    if (key == Qt::Key_V) tile = m_copiedTexture;
    if (key == 0) {
        if (!chooseTexture) return;
        // Open one chooser for the entire fixed target set.
        const bool captured = m_captured;
        m_timer.stop();
        releaseLook();
        tile = chooseTexture(texture(targets.front()));
        if (m_active) {
            setFocus();
            if (captured) captureLook();
            m_clock.restart();
            m_timer.start();
        }
        if (!tile || !m_active) return;
    }
    auto candidate = m_snapshot;
    const bool horizontal = key == Qt::Key_Left || key == Qt::Key_Right;
    const int pan = (key == Qt::Key_Left || key == Qt::Key_Down) ? 1 : -1;
    const bool enlarge = key == Qt::Key_Right || key == Qt::Key_Up;
    const auto wrap = [](int value) { return (value + 256) % 256; };
    for (const auto &target : targets) {
        if (target.kind == DUKE_SURFACE_SPRITE) {
            // Sprite texture choice/paste is supported; surface UV shortcuts
            // do not reinterpret sprite dimensions or offsets.
            if (!tile) continue;
            auto sprite = candidate.sprites()[target.id];
            sprite.texture = *tile;
            candidate.setSprite(target.id, sprite);
        } else if (target.kind == DUKE_SURFACE_WALL) {
            const auto &wall = candidate.walls()[target.id];
            auto side = target.reversed ? wall.reverseSide : wall.forwardSide;
            if (tile) side.texture = *tile;
            else if (key == Qt::Key_R) {
                side.xrepeat = candidate.defaultWallXRepeat(target.id);
                side.yrepeat = 8;
            } else if (scale) {
                int &repeat = horizontal ? side.xrepeat : side.yrepeat;
                repeat = std::clamp(repeat + (enlarge ? -1 : 1), 1, 255);
            } else {
                int &offset = horizontal ? side.xpanning : side.ypanning;
                offset = wrap(offset + pan);
            }
            candidate.setWallSide(target.id, target.reversed, side);
        } else {
            if (key == Qt::Key_R) continue;
            auto sector = candidate.sectors()[target.id];
            const bool floor = target.kind == DUKE_SURFACE_FLOOR;
            if (tile) (floor ? sector.floorTexture : sector.ceilingTexture) = *tile;
            else if (scale) {
                int &flags = floor ? sector.floorstat : sector.ceilingstat;
                if (enlarge) flags &= ~8;
                else flags |= 8;
            } else {
                int &offset = floor
                    ? (horizontal ? sector.floorxpanning : sector.floorypanning)
                    : (horizontal ? sector.ceilingxpanning : sector.ceilingypanning);
                offset = wrap(offset + pan);
            }
            candidate.setSector(target.id, sector);
        }
    }
    commitSurfaceEdit(std::move(candidate), key == Qt::Key_R ? "Reset texture scale"
        : tile ? "Change texture" : scale ? "Change texture size" : "Change texture offset");
}

QSize MapView3D::alignmentTextureSize(int tile) const
{
    // Use the archive actually rendered, not the browser's merged archive set.
    // Like the renderer, later ART entries override earlier tile definitions.
    std::unique_ptr<DukeGrpFile, decltype(&duke_grp_free)> grp(duke_grp_new(), duke_grp_free);
    if (!grp || !duke_grp_open_filename(grp.get(), QFile::encodeName(m_archive).constData())
        || !duke_grp_read_entries_sparse(grp.get())) {
        return {};
    }
    for (auto index = grp->header.entry_count; index > 0; --index) {
        const auto *entry = duke_grp_get_entry_by_index(grp.get(), index - 1);
        if (!entry || !QString::fromLatin1(entry->filename).endsWith(".ART", Qt::CaseInsensitive)) {
            continue;
        }
        void *bytes = nullptr;
        const auto size = duke_grp_get_file_data_by_index(grp.get(), index - 1, &bytes);
        std::unique_ptr<DukeArtFile, decltype(&duke_art_free)> art(duke_art_new(), duke_art_free);
        if (!bytes || size == size_t(-1) || !art || !duke_art_open_memory(art.get(), bytes, size)
            || !duke_art_read_tiles_sparse(art.get())) {
            continue;
        }
        const auto *texture = duke_art_get_tile_by_number(art.get(), tile);
        if (texture && texture->width > 0 && texture->height > 0) {
            void *pixels = nullptr;
            if (duke_art_get_tile_data_by_number(art.get(), tile, &pixels) == size_t(-1)) {
                return {};
            }
            return {texture->width, texture->height};
        }
    }
    return {};
}

void MapView3D::alignSelectedWallTextures()
{
    if (!m_active || !m_renderer) {
        return;
    }
    repaint();
    DukeSurfaceHit hit{};
    const auto reference = duke_renderer_get_hovered_surface(m_renderer, &hit)
        ? selectionFromHit(hit) : std::optional<SurfaceSelection>{};
    const auto report = [this](const QString &message) {
        if (statusMessage) { statusMessage(message); }
    };
    if (!reference || reference->kind != DUKE_SURFACE_WALL
        || std::find(m_selection.begin(), m_selection.end(), *reference) == m_selection.end()) {
        report("Point at a selected wall to use it as the alignment reference.");
        return;
    }
    std::vector<WallTextureTarget> targets;
    int otherSurfaces = 0;
    for (const auto &entry : m_selection) {
        if (entry.kind == DUKE_SURFACE_WALL) {
            targets.push_back({entry.id, entry.reversed});
        } else {
            ++otherSurfaces;
        }
    }
    const auto &wall = m_snapshot.walls()[reference->id];
    const int tile = (reference->reversed ? wall.reverseSide : wall.forwardSide).texture;
    auto candidate = m_snapshot;
    auto result = alignWallTextures(candidate, targets, {reference->id, reference->reversed},
        alignmentTextureSize(tile));
    if (!result.error.isEmpty()) {
        report(result.error);
        return;
    }
    if (result.changed) {
        if (continuousEditChanged) { continuousEditChanged({}); }
        if (!commitSurfaceEdit(std::move(candidate), "Align wall textures")) {
            return;
        }
    }
    QString message = QString("Aligned %1 wall(s); reference and texture scales preserved.").arg(result.aligned);
    if (otherSurfaces) {
        result.skipped += otherSurfaces;
        result.reasons.append("non-wall surfaces");
    }
    if (result.skipped) {
        message += QString(" Skipped %1: %2.").arg(result.skipped).arg(result.reasons.join(", "));
    }
    if (result.approximate) { message += " Some offsets are approximate (Build panning limits)."; }
    if (result.closingSeam) { message += " A closing seam remains; scale was not stretched."; }
    report(message);
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
