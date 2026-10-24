#include "texturevalueedit.h"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>

void TextureValueEdit::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasFormat("application/x-dukebuilder-tile")) {
        event->acceptProposedAction();
        return;
    }
    QLineEdit::dragEnterEvent(event);
}

void TextureValueEdit::dropEvent(QDropEvent *event)
{
    bool valid = false;
    const int tile = QString::fromUtf8(
        event->mimeData()->data("application/x-dukebuilder-tile")).toInt(&valid);
    if (valid) {
        setText(QString::number(tile));
        if (tileDropped) tileDropped(tile);
        event->acceptProposedAction();
        return;
    }
    QLineEdit::dropEvent(event);
}
