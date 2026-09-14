#include "recovery.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace {
constexpr qint64 maximumBytes = 64 * 1024 * 1024;
const QByteArray magic("DUKE-RECOVERY-1\n");
// A fixed stream version and explicit integer widths make snapshots independent
// of compiler layout, platform word size, and the Qt version used at runtime.
class Archive {
public:
    QDataStream &stream;
    bool reading;
    template<class T> void field(T &value) {
        if constexpr (std::is_integral_v<T>) {
            qint64 wire = static_cast<qint64>(value);
            if (reading) {
                stream >> wire;
                if (static_cast<long double>(wire) < std::numeric_limits<T>::lowest()
                    || static_cast<long double>(wire) > std::numeric_limits<T>::max()) fail();
                value = static_cast<T>(wire);
            } else stream << wire;
        } else if constexpr (std::is_floating_point_v<T>) {
            double wire = value;
            if (reading) { stream >> wire; if (!std::isfinite(wire)) fail(); value = wire; }
            else stream << wire;
        } else {
            if (reading) stream >> value; else stream << value;
        }
        if (stream.status() != QDataStream::Ok) fail();
    }
    void field(QPointF &point) {
        qreal x = point.x(), y = point.y(); field(x); field(y); point = {x,y};
    }
    template<class T> void field(std::optional<T> &value) {
        bool present = value.has_value(); field(present);
        if (reading) { if (present) value = T{}; else value.reset(); }
        if (present) field(*value);
    }
    template<class T> void field(std::vector<T> &values) {
        quint32 count = values.size(); field(count);
        if (count > 1000000 || (reading && quint64(count) > quint64(stream.device()->bytesAvailable()))) fail();
        if (reading) values.resize(count);
        for (auto &value : values) field(value);
    }
    void field(MapDocument::Vertex &value) {
        field(value.position);
    }
    void field(MapDocument::WallSide &value) {
        field(value.texture);
        field(value.overlayTexture);
        field(value.shade);
        field(value.palette);
        field(value.xrepeat);
        field(value.yrepeat);
        field(value.xpanning);
        field(value.ypanning);
        field(value.cstat);
        field(value.hitag);
        field(value.lotag);
        field(value.extra);
    }
    void field(MapDocument::Wall &value) {
        field(value.start);
        field(value.end);
        field(value.forwardSide);
        field(value.reverseSide);
        field(value.forwardSector);
        field(value.reverseSector);
    }
    void field(MapDocument::Sector &value) {
        field(value.walls);
        field(value.vertices);
        field(value.loopStarts);
        field(value.floorz);
        field(value.ceilingz);
        field(value.floorTexture);
        field(value.ceilingTexture);
        field(value.hitag);
        field(value.lotag);
        field(value.floorstat);
        field(value.ceilingstat);
        field(value.floorheinum);
        field(value.ceilingheinum);
        field(value.floorshade);
        field(value.ceilingshade);
        field(value.floorpal);
        field(value.ceilingpal);
        field(value.floorxpanning);
        field(value.floorypanning);
        field(value.ceilingxpanning);
        field(value.ceilingypanning);
        field(value.visibility);
        field(value.extra);
        field(value.filler);
    }
    void field(MapDocument::Sprite &value) {
        field(value.position);
        field(value.z);
        field(value.angle);
        field(value.texture);
        field(value.hitag);
        field(value.lotag);
        field(value.cstat);
        field(value.shade);
        field(value.palette);
        field(value.clipdist);
        field(value.xrepeat);
        field(value.yrepeat);
        field(value.xoffset);
        field(value.yoffset);
        field(value.statnum);
        field(value.owner);
        field(value.xvel);
        field(value.yvel);
        field(value.zvel);
        field(value.extra);
        field(value.filler);
        field(value.sectorId);
    }
    void field(MapDocument::PlayerStart &value) {
        field(value.position);
        field(value.z);
        field(value.angle);
        field(value.sectorId);
    }
    [[noreturn]] static void fail() { throw std::runtime_error("Invalid or unsupported recovery snapshot."); }
};
}

QByteArray RecoveryCodec::encode(RecoverySnapshot snapshot)
{
    QByteArray payload;
    QDataStream stream(&payload, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    Archive archive{stream, false};
    auto &d = snapshot.document;
    archive.field(d.m_vertices); archive.field(d.m_walls); archive.field(d.m_sectors);
    archive.field(d.m_sprites); archive.field(d.m_playerStart); archive.field(d.m_complexTopology);
    archive.field(snapshot.drawingPoints); archive.field(snapshot.sourceFilename); archive.field(snapshot.timestamp);
    if (payload.size() > maximumBytes) throw std::runtime_error("Recovery snapshot exceeds 64 MiB.");
    return magic + QCryptographicHash::hash(payload, QCryptographicHash::Sha256) + payload;
}

bool RecoveryCodec::decode(const QByteArray &data, RecoverySnapshot &snapshot, QString &error)
{
    error.clear();
    try {
        if (data.size() > maximumBytes + magic.size() + 32 || !data.startsWith(magic)
            || data.size() < magic.size() + 32) Archive::fail();
        auto payload = data.mid(magic.size() + 32);
        if (QCryptographicHash::hash(payload, QCryptographicHash::Sha256) != data.mid(magic.size(),32)) Archive::fail();
        QDataStream stream(&payload, QIODevice::ReadOnly);
        stream.setVersion(QDataStream::Qt_6_0);
        Archive archive{stream, true};
        RecoverySnapshot candidate;
        auto &d = candidate.document;
        archive.field(d.m_vertices); archive.field(d.m_walls); archive.field(d.m_sectors);
        archive.field(d.m_sprites); archive.field(d.m_playerStart); archive.field(d.m_complexTopology);
        archive.field(candidate.drawingPoints); archive.field(candidate.sourceFilename); archive.field(candidate.timestamp);
        if (!stream.atEnd()) Archive::fail();
        // Allow unfinished/game-invalid maps, but reject unsafe internal references.
        for (const auto &w : d.m_walls) {
            if (w.start >= d.m_vertices.size() || w.end >= d.m_vertices.size()
                || (w.forwardSector && *w.forwardSector >= d.m_sectors.size())
                || (w.reverseSector && *w.reverseSector >= d.m_sectors.size())) Archive::fail();
        }
        for (const auto &s : d.m_sectors) {
            if (s.walls.size() != s.vertices.size() || s.walls.empty()) Archive::fail();
            for (auto id : s.walls) if (id >= d.m_walls.size()) Archive::fail();
            for (auto id : s.vertices) if (id >= d.m_vertices.size()) Archive::fail();
            std::size_t previous = 0;
            for (std::size_t i = 0; i < s.loopStarts.size(); ++i) {
                if (s.loopStarts[i] >= s.walls.size() || (i == 0 && s.loopStarts[i] != 0)
                    || (i && s.loopStarts[i] <= previous)) Archive::fail();
                previous = s.loopStarts[i];
            }
        }
        snapshot = std::move(candidate);
        return true;
    } catch (const std::exception &exception) {
        error = QString::fromUtf8(exception.what()); return false;
    }
}

RecoveryFile::RecoveryFile(const QString &path)
    : m_path(path), m_lock(path + ".lock"), m_locked(false)
{
    // A running process's lock must never expire just because it is old.
    m_lock.setStaleLockTime(0);
    m_locked = m_lock.tryLock(0);
}

bool RecoveryFile::write(const RecoverySnapshot &snapshot, QString &error)
{
    error.clear();
    if (!m_locked) { error = "Recovery directory is unavailable or this session is locked."; return false; }
    try {
        const auto data = RecoveryCodec::encode(snapshot);
        QSaveFile file(m_path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            error = file.errorString(); return false;
        }
        return true;
    } catch (const std::exception &exception) {
        error = QString::fromUtf8(exception.what()); return false;
    }
}

bool RecoveryFile::read(RecoverySnapshot &snapshot, QString &error) const
{
    if (!m_locked) { error = "Recovery snapshot belongs to an active session."; return false; }
    QFile file(m_path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return false; }
    if (file.size() > maximumBytes + magic.size() + 32) { error = "Recovery snapshot is too large."; return false; }
    return RecoveryCodec::decode(file.readAll(), snapshot, error);
}

bool RecoveryFile::remove()
{
    return m_locked && (!QFile::exists(m_path) || QFile::remove(m_path));
}

QString RecoveryFile::directory()
{
    const auto path = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recovery";
    QDir().mkpath(path);
    return path;
}

QString RecoveryFile::newPath(const QString &directory)
{
    return QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".recovery");
}

QStringList RecoveryFile::candidates(const QString &directory)
{
    QStringList result;
    for (const auto &name : QDir(directory).entryList({"*.recovery"}, QDir::Files, QDir::Time))
        result.push_back(QDir(directory).filePath(name));
    return result;
}
