#pragma once

#include <QImage>
#include <QJsonObject>
#include <QPointF>
#include <QSizeF>
#include <QTransform>

// Editor-only state. The source image remains an external file; copies of the
// decoded image share storage across undo snapshots.
struct ReferenceImage {
    QString filename;
    QImage image;
    QPointF position;
    QSizeF size;
    qreal rotation = 0;
    qreal opacity = 0.5;
    bool visible = true;
    bool locked = true;
    bool keepAspect = true;

    bool operator==(const ReferenceImage &other) const {
        return filename == other.filename && image.cacheKey() == other.image.cacheKey()
            && position == other.position && size == other.size && rotation == other.rotation
            && opacity == other.opacity && visible == other.visible && locked == other.locked
            && keepAspect == other.keepAspect;
    }
    bool operator!=(const ReferenceImage &other) const { return !(*this == other); }
    QTransform transform() const {
        QTransform result;
        result.translate(position.x(), position.y());
        result.rotate(rotation);
        result.scale(size.width(), size.height());
        return result;
    }
    QJsonObject metadata(const QString &directory = {}) const;
    static bool fromMetadata(const QJsonObject &json, const QString &directory,
                             ReferenceImage &result, QString &error);
};
