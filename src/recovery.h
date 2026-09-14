#pragma once
#include "mapdocument.h"
#include <QDateTime>
#include <QLockFile>
#include <memory>

struct RecoverySnapshot {
    MapDocument document;
    std::vector<QPointF> drawingPoints;
    QString sourceFilename;
    QDateTime timestamp;
};

// A lock is held for this object's lifetime. Destruction deliberately keeps the
// snapshot; remove() is called only after saving or explicitly discarding work.
class RecoveryFile {
public:
    explicit RecoveryFile(const QString &path);
    bool locked() const { return m_locked; }
    bool write(const RecoverySnapshot &snapshot, QString &error);
    bool read(RecoverySnapshot &snapshot, QString &error) const;
    bool remove();
    static QString directory();
    static QString newPath(const QString &directory);
    static QStringList candidates(const QString &directory);
private:
    QString m_path;
    QLockFile m_lock;
    bool m_locked;
};

class RecoveryCodec {
public:
    static QByteArray encode(RecoverySnapshot snapshot);
    static bool decode(const QByteArray &data, RecoverySnapshot &snapshot, QString &error);
};
