#include "mapeditor.h"

#include <QApplication>
#include <QMouseEvent>
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
    MapEditor editor;
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
    };
    MapDocument tror;
    QString error;
    require(tror.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true), "TROR upper room");
    tror.setPlayerStartPosition({512,512});
    require(tror.extendTror(0, true, 8192, error).has_value(), "TROR lower room");
    {
        auto layersMap = tror;
        require(layersMap.addPolyline({{4096,0},{5120,0},{5120,1024},{4096,1024}}, true,
                                      &error, std::set<MapDocument::SectorId>{}), "Layer panel unrelated room");
        auto lower = layersMap.sectors()[1];
        lower.floorstat |= 2; lower.floorheinum = 256;
        layersMap.setSector(1, lower);
        editor.recoverDocument(layersMap, {});
        require(editor.trorLayers().empty(), "Layer list starts hidden without a TROR selection");
        editor.setMode(MapEditor::Mode::Sectors);
        require(editor.setEditingScope(std::set<MapDocument::SectorId>{0}), "Layer panel upper scope");
        editor.centerOn(512,512);
        click({512,512});
        auto layers = editor.trorLayers();
        require(layers.size() == 2 && layers[0].sectors == std::set<MapDocument::SectorId>{0}
                && layers[1].sectors == std::set<MapDocument::SectorId>{1} && layers[0].active,
                "Layer rows follow top-to-bottom TROR connections and show active scope");
        require(layers[1].floorZ == lower.floorz + 1024, "Layer height range includes slope extrema");
        const auto camera = editor.mapToScene(editor.viewport()->rect().center());
        require(editor.activateTrorLayer(1), "Layer row activates lower layer");
        require(editor.trorLayers()[1].active && editor.document() == layersMap
                && editor.undoStack()->count() == 0
                && editor.mapToScene(editor.viewport()->rect().center()) == camera,
                "Layer activation keeps document, history and camera unchanged");
        editor.setMode(MapEditor::Mode::Vertices);
        click({0,0});
        require(editor.trorLayers().size() == 2, "Layer list persists when selecting a vertex");
        editor.clearEditingScope();
        layers = editor.trorLayers();
        require(layers.size() == 2 && !layers[0].active && !layers[1].active,
                "Show all preserves stack context and clears isolated layer");
        editor.setMode(MapEditor::Mode::Sectors);
        editor.centerOn(4608,512);
        click({4608,512});
        require(editor.trorLayers().empty(), "Selecting unrelated geometry hides layer list");
        editor.newMap();
        require(editor.trorLayers().empty(), "New map resets layer context");
    }
    {
        MapDocument branched;
        require(branched.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true), "Branch first room");
        require(branched.addPolyline({{1024,0},{2048,0},{2048,1024},{1024,1024}}, true), "Branch adjoining room");
        require(branched.extendTror(0, true, 8192, error).has_value(), "Branch first lower room");
        require(branched.extendTror(1, true, 8192, error).has_value(), "Branch second lower room");
        editor.recoverDocument(branched, {});
        require(editor.setEditingScope(std::set<MapDocument::SectorId>{0,1}), "Branch active layer");
        const auto layers = editor.trorLayers();
        require(layers.size() == 3 && layers[0].sectors == std::set<MapDocument::SectorId>({0,1})
                && layers[0].below == std::set<MapDocument::SectorId>({2,3}),
                "Horizontal rooms group together and branching lower connections remain distinct");
    }
}
