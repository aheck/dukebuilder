#pragma once

#include <QDialog>
#include <QString>

class MapDocument;

class MapInfoWindow final : public QDialog
{
public:
    explicit MapInfoWindow(const MapDocument &document, const QString &filename,
                           bool unsavedChanges, QWidget *parent = nullptr);
};
