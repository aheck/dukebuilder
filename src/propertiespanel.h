#pragma once

#include "mapeditor.h"
#include <QWidget>

class PropertyValueDelegate;
class QGroupBox;
class QLabel;
class QPushButton;
class QTimer;
class QTreeWidget;
class TextureBrowserWindow;

// The editor and texture browser are supplied by the owning window.
class PropertiesPanel final : public QWidget
{
public:
    PropertiesPanel(MapEditor *editor, TextureBrowserWindow *textureBrowser,
                             QWidget *parent = nullptr);
    void setProperties(std::optional<MapEditor::SelectionProperties> properties);
    void setMapViewActive(bool active);

private:
    void refreshLayers();
    void updateTexturePreview(QPushButton *preview, int tile, const QString &surface);

    MapEditor *m_editor = nullptr;
    TextureBrowserWindow *m_textureBrowserWindow = nullptr;
    QTreeWidget *m_propertiesControl = nullptr;
    PropertyValueDelegate *m_propertyDelegate = nullptr;
    QGroupBox *m_trorLayersPanel = nullptr;
    QLabel *m_layersStatus = nullptr;
    QTreeWidget *m_layersList = nullptr;
    QPushButton *m_showAllLayers = nullptr;
    QTimer *m_layersRefresh = nullptr;
    QPushButton *m_wallTexturePreview = nullptr;
    QWidget *m_wallTexturePreviews = nullptr;
    QLabel *m_wallTextureLabel = nullptr;
    QWidget *m_oppositeTexturePanel = nullptr;
    QLabel *m_oppositeTextureLabel = nullptr;
    QPushButton *m_oppositeTexturePreview = nullptr;
    QWidget *m_spriteTexturePreviews = nullptr;
    QPushButton *m_spriteTexturePreview = nullptr;
    QWidget *m_sectorTexturePreviews = nullptr;
    QPushButton *m_floorTexturePreview = nullptr;
    QPushButton *m_ceilingTexturePreview = nullptr;
    bool m_mapViewActive = true;
};
