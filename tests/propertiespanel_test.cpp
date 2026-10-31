#include "propertiespanel.h"
#include "texturebrowserwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QGroupBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QtPlugin>
#include <cstdlib>
#include <iostream>

#ifdef DUKE_BUILDER_STATIC_MINIMAL_PLUGIN
Q_IMPORT_PLUGIN(QMinimalIntegrationPlugin)
#endif

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(EXIT_FAILURE); }
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir settings;
    require(settings.isValid(), "Temporary settings directory");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    app.setOrganizationName("DukeBuilderTests");
    app.setApplicationName("PropertiesPanel");
    MapEditor editor;
    TextureBrowserWindow textures;
    auto panel = std::make_unique<PropertiesPanel>(&editor, &textures);
    editor.setPropertiesCallback([guard = QPointer<PropertiesPanel>(panel.get())](auto properties) {
        if (guard) guard->setProperties(std::move(properties));
    });
    auto *tree = panel->findChild<QTreeWidget *>("PropertiesControl");
    require(tree && tree->topLevelItemCount() == 0, "Empty selection has no property rows");
    const auto row = [&](const QString &name) -> QTreeWidgetItem * {
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->text(0) == name) return tree->topLevelItem(i);
        std::cerr << "Missing property: " << name.toStdString() << '\n';
        std::exit(EXIT_FAILURE);
    };
    editor.resize(1000, 800);
    editor.show();
    editor.setZoomPercent(200);
    const auto click = [&](QPointF point) {
        const QPoint local = editor.mapFromScene(point);
        const QPoint global = editor.viewport()->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &release);
        QApplication::processEvents();
    };
    MapDocument map;
    require(map.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true), "Room fixture");
    map.setSectorFloorTexture(0, 42);
    const auto sprite = map.addSprite({256,256});
    map.setSpriteTexture(sprite, 1680);
    editor.recoverDocument(map, {});
    editor.setMode(MapEditor::Mode::Sectors);
    editor.centerOn(512,512);
    click({512,512});
    require(row("Floor texture")->text(1) == "42", "Sector selection renders properties");
    auto *floor = panel->findChild<QPushButton *>("FloorTexturePreview");
    require(floor && floor->property("tile").toInt() == 42 && floor->text().contains("Unavailable"),
            "Missing texture keeps its tile and fallback preview");
    row("Advanced")->setExpanded(true);
    row("Floor shade")->setText(1, "12");
    QApplication::processEvents();
    require(editor.document().sectors()[0].floorshade == 12 && editor.undoStack()->count() == 1,
            "Property edit updates document through undo history");
    require(row("Advanced")->isExpanded(), "Expansion survives property rebuild");
    row("Floor shade")->setText(1, "13");
    panel->setProperties(std::nullopt);
    QApplication::processEvents();
    require(editor.document().sectors()[0].floorshade == 12,
            "Queued text edit from a retired row cannot commit");
    editor.undo();
    require(editor.document().sectors()[0].floorshade == 0, "Property edit can be undone");
    click({512,512});
    row("Floor flags")->child(0)->setCheckState(1, Qt::Checked);
    QApplication::processEvents();
    require(editor.document().sectors()[0].floorstat & 1, "Flag checkbox updates selected sector");
    row("Floor flags")->child(0)->setCheckState(1, Qt::Unchecked);
    panel->setProperties(std::nullopt);
    QApplication::processEvents();
    require(editor.document().sectors()[0].floorstat & 1,
            "Queued checkbox edit from a retired row cannot commit");

    editor.setMode(MapEditor::Mode::Lines);
    click({512,0});
    require(row("Length")->text(1) == "1024", "Wall selection renders length");
    auto *side = qobject_cast<QComboBox *>(tree->itemWidget(row("Side"), 1));
    require(side && side->count() == 1 && !side->isEnabled(), "One-sided wall has a fixed side");
    row("Shade")->setText(1, "9");
    QApplication::processEvents();
    require(row("Shade")->text(1) == "9", "Wall edit survives document refresh");

    editor.setMode(MapEditor::Mode::Sprites);
    click({256,256});
    require(row("Texture")->text(1) == "1680", "Sprite selection replaces sector properties");
    require(panel->findChild<QWidget *>("SectorTexturePreviews")->isHidden(), "Sector previews hide for sprites");
    auto *alignment = qobject_cast<QComboBox *>(tree->itemWidget(row("Alignment"), 1));
    require(alignment, "Sprite alignment choice exists");
    alignment->setCurrentIndex(alignment->findData(1));
    alignment->activated(alignment->currentIndex());
    QApplication::processEvents();
    require((editor.document().sprites()[sprite].cstat & 48) == 16, "Queued choice commits to sprite");
    alignment = qobject_cast<QComboBox *>(tree->itemWidget(row("Alignment"), 1));
    alignment->setCurrentIndex(alignment->findData(2));
    alignment->activated(alignment->currentIndex());
    panel->setProperties(std::nullopt);
    QApplication::processEvents();
    require((editor.document().sprites()[sprite].cstat & 48) == 16,
            "Queued choice from a retired row cannot commit");

    QString error;
    require(map.extendTror(0, true, 8192, error).has_value(), "TROR fixture");
    editor.recoverDocument(map, {});
    editor.setMode(MapEditor::Mode::Sectors);
    require(editor.setEditingScope(std::set<MapDocument::SectorId>{0}), "Upper layer editing scope");
    click({512,512});
    auto *layers = panel->findChild<QTreeWidget *>("TrorLayersList");
    auto *layersPanel = panel->findChild<QGroupBox *>("TrorLayersPanel");
    require(layers->topLevelItemCount() == 2 && !layersPanel->isHidden(), "TROR layers render for selection");
    panel->setMapViewActive(false);
    QApplication::processEvents();
    require(layersPanel->isHidden(), "TROR layers hide in 3D");
    panel->setMapViewActive(true);
    QApplication::processEvents();
    require(!layersPanel->isHidden(), "TROR layers return in 2D");
    layers->setCurrentItem(layers->topLevelItem(1));
    QApplication::processEvents();
    require(editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({1}),
            "Layer selection changes editing scope");
    panel->findChild<QPushButton *>("ShowAllTrorLayers")->click();
    QApplication::processEvents();
    require(!editor.editingScope(), "Show all layers clears scope");
    panel.reset();
    editor.newMap();
    QApplication::processEvents();
}
