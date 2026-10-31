#include "mapeditor.h"
#include "mapsave.h"

#include <QApplication>
#include <QCursor>
#include <QKeyEvent>
#include <QFocusEvent>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QInputDialog>
#include <QComboBox>
#include <QTimer>
#include <QGraphicsItem>
#include <QGraphicsSceneHoverEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QJsonDocument>
#include <QFile>
#include <QPushButton>
#include <QMessageBox>
#include <QAbstractButton>
#include <QtPlugin>
#include <algorithm>
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
    const auto click = [&](QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint local = editor.mapFromScene(point);
        const QPoint global = editor.viewport()->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::LeftButton, Qt::LeftButton, modifiers);
        QApplication::sendEvent(editor.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::LeftButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(editor.viewport(), &release);
    };
    const auto rightClick = [&](QPointF point) {
        const QPoint local = editor.mapFromScene(point);
        const QPoint global = editor.viewport()->mapToGlobal(local);
        QMouseEvent press(QEvent::MouseButtonPress, local, global,
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &press);
        QMouseEvent release(QEvent::MouseButtonRelease, local, global,
                            Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &release);
    };
    // Shape tools preview without changing the map, then commit one undo step.
    editor.setZoomPercent(200);
    editor.centerOn(0, 0);
    QApplication::processEvents();
    for (const auto tool : {MapEditor::DrawTool::Rectangle, MapEditor::DrawTool::Circle,
                            MapEditor::DrawTool::Polygon}) {
        editor.newMap();
        editor.setDrawTool(tool);
        editor.setShapeSides(tool == MapEditor::DrawTool::Circle ? 16 : 5);
        click({0, 0});
        require(editor.document().sectors().empty() && !editor.drawingPoints().empty(),
                "Shape anchor only starts a preview");
        require(!editor.canAutosave(), "Do not recover a shape anchor as a freeform drawing");
        click({0, 0});
        require(editor.document().sectors().empty() && !editor.drawingPoints().empty(),
                "Degenerate shape remains adjustable");
        click({1024, 1024});
        require(editor.document().sectors().size() == 1 && editor.drawingPoints().empty(),
                "Second shape click creates a sector");
        const auto count = tool == MapEditor::DrawTool::Rectangle ? 4u
                         : tool == MapEditor::DrawTool::Circle ? 16u : 5u;
        require(editor.document().walls().size() == count, "Shape has requested wall count");
        require(editor.undoStack()->count() == 1, "Shape creates one undo entry");
        editor.undo();
        require(editor.document().sectors().empty(), "Undo removes the complete shape");
        editor.redo();
        require(editor.document().walls().size() == count, "Redo restores the complete shape");
        click({-2048, -2048});
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&editor, &escape);
        require(editor.drawingPoints().empty() && editor.document().walls().size() == count,
                "Escape cancels only the shape preview");
    }
    editor.newMap();
    editor.setDrawTool(MapEditor::DrawTool::Freeform);

    editor.setDrawTool(MapEditor::DrawTool::Rectangle);
    auto *gridScene = static_cast<MapScene *>(editor.scene());
    gridScene->setGridAngle(0.4);
    click({0, 0});
    click(gridScene->fromGrid({1024, 512}), Qt::ShiftModifier);
    require(editor.document().walls().size() == 4, "Rotated square is created");
    const auto &square = editor.document().vertices();
    const auto edgeA = square[1].position - square[0].position;
    const auto edgeB = square[2].position - square[1].position;
    require(std::abs(QPointF::dotProduct(edgeA, edgeB)) < 0.01
            && std::abs(QLineF({}, edgeA).length() - QLineF({}, edgeB).length()) < 0.01,
            "Shift rectangle keeps equal perpendicular sides on rotated grid");
    gridScene->setGridAngle(0);
    editor.newMap();
    click({0, 0});
    click({1024, 1024});
    click({1024, 0});
    click({2048, 1024});
    require(editor.document().sectors().size() == 2, "Rectangle attaches to neighboring sector");
    require(std::count_if(editor.document().walls().begin(), editor.document().walls().end(),
            [](const auto &wall) { return wall.forwardSector && wall.reverseSector; }) == 1,
            "Adjacent rectangles share a portal wall");
    editor.newMap();
    editor.setDrawTool(MapEditor::DrawTool::Freeform);

    MapDocument source;
    require(source.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true), "Create source room");
    source.setPlayerStartPosition({512,512});
    source.setSectorFloorZ(0, -2048);
    source.setSectorCeilingZ(0, -16384);
    {
        MapEditor modes;
        modes.resize(1000,800); modes.show();
        require(modes.geometryMode() == MapEditor::GeometryMode::Connect, "Connect is the default");
        const auto clickMode = [&](QPointF point) {
            const auto local = modes.mapFromScene(point);
            const auto global = modes.viewport()->mapToGlobal(local);
            QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(modes.viewport(), &press);
            QApplication::sendEvent(modes.viewport(), &release);
        };
        for (auto tool : {MapEditor::DrawTool::Freeform, MapEditor::DrawTool::Rectangle,
                          MapEditor::DrawTool::Circle, MapEditor::DrawTool::Polygon}) {
            modes.recoverDocument(source, {});
            modes.setMode(MapEditor::Mode::Draw);
            modes.setGeometryMode(MapEditor::GeometryMode::Independent);
            modes.setDrawTool(tool);
            modes.setZoomPercent(400); modes.centerOn(512,512);
            QApplication::processEvents();
            if (tool == MapEditor::DrawTool::Freeform) {
                clickMode({0,0}); clickMode({1024,0}); clickMode({1024,1024}); clickMode({0,1024}); clickMode({0,0});
            } else {
                clickMode({512,512}); clickMode({768,768});
            }
            require(modes.document().sectors().size() == 2 && modes.document().sectors()[0] == source.sectors()[0],
                    "Every drawing tool leaves existing sectors intact in Independent mode");
            require(modes.document().vertices().size() > source.vertices().size(), "Independent drawing creates fresh vertices");
            modes.undo(); require(modes.document() == source, "Independent drawing is one undo step");
        }
        modes.setDrawTool(MapEditor::DrawTool::Rectangle);
        modes.setGeometryMode(MapEditor::GeometryMode::Connect);
        clickMode({1024,0}); clickMode({2048,1024});
        require(std::count_if(modes.document().walls().begin(), modes.document().walls().end(),
                [](const auto &w) { return w.isTwoSided(); }) == 1, "Connect drawing joins the existing room");

        auto stacked = source;
        QString error;
        require(stacked.pasteSectors(source, {}, error), "Height overlap fixture");
        stacked.setSectorCeilingZ(1,-32768); stacked.setSectorFloorZ(1,-24576);
        require(stacked.pasteSectors(source, {}, error), "Same-height overlap fixture");
        modes.recoverDocument(stacked, {}); modes.setMode(MapEditor::Mode::Sectors);
        const auto sectorItem = [&](int id) -> QGraphicsItem * {
            for (auto *item : modes.scene()->items()) {
                if (item->data(Qt::UserRole+2).isValid() && item->data(Qt::UserRole+2).toInt() == id) return item;
            }
            return nullptr;
        };
        sectorItem(0)->setSelected(true);
        modes.focusSelectedHeightRange();
        require(sectorItem(1)->isVisible() && !sectorItem(1)->isEnabled() && sectorItem(1)->opacity() < 1,
                "Height focus dims and disables other heights");
        require(sectorItem(0)->isEnabled() && sectorItem(2)->isEnabled(), "Height focus keeps same-height overlaps editable");
        sectorItem(2)->setSelected(true); modes.hideSelectedSectors();
        require(!sectorItem(2)->isVisible() && !modes.editingScope()->count(2), "Explicitly hidden sectors disappear and leave editing scope");
        modes.showHiddenSectors();
        require(sectorItem(2)->isVisible() && sectorItem(2)->isEnabled() && !sectorItem(1)->isEnabled(),
                "Show hidden restores sectors without clearing height focus");
        modes.toggleSectorIsolation();
        require(!modes.editingScope(), "Isolation toggle restores the previous view");
        require(modes.document() == stacked && modes.undoStack()->count() == 0, "Visibility controls never edit map data");

        // The overlap dialog previews a candidate without changing selection,
        // and can hide precisely that sector even at identical heights.
        bool previewed = false;
        QTimer chooserTimer;
        QObject::connect(&chooserTimer, &QTimer::timeout, [&] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (!dialog || dialog->windowTitle() != "Choose edit target") return;
            const auto choices = dialog->comboBoxItems();
            for (const auto &choice : choices) {
                if (choice.startsWith("Sector 2 ")) dialog->setTextValue(choice);
            }
            for (auto *item : modes.scene()->items()) {
                if (item->zValue() == 25 && !item->boundingRect().isEmpty()) previewed = true;
            }
            auto *action = dialog->findChild<QComboBox *>("OverlapAction");
            require(action, "Overlap chooser offers sector actions");
            action->setCurrentIndex(1);
            dialog->accept();
        });
        modes.centerOn(512,512); QApplication::processEvents();
        chooserTimer.start(1); clickMode({512,512}); chooserTimer.stop();
        require(previewed && !sectorItem(2)->isVisible() && sectorItem(0)->isVisible(),
                "Overlap chooser previews and hides only the chosen sector");
        require(modes.document() == stacked, "Overlap preview and hide leave geometry unchanged");
        modes.showHiddenSectors();

        for (bool independent : {true,false}) {
            modes.recoverDocument(source, {}); modes.setMode(MapEditor::Mode::Sectors);
            modes.setGeometryMode(MapEditor::GeometryMode::Connect);
            sectorItem(0)->setSelected(true); modes.copySelectedSectors();
            modes.centerOn(1024,512); QApplication::processEvents();
            QCursor::setPos(modes.viewport()->mapToGlobal(modes.mapFromScene({1024,0})));
            modes.pasteCopiedSectors();
            require(modes.hasFloatingPaste(), "Paste preview is pending");
            modes.setGeometryMode(independent ? MapEditor::GeometryMode::Independent : MapEditor::GeometryMode::Connect);
            require(modes.hasFloatingPaste(), "Connection choice can change during paste preview");
            modes.setMode(MapEditor::Mode::Draw);
            require(!modes.hasFloatingPaste() && std::count_if(modes.document().walls().begin(), modes.document().walls().end(),
                    [](const auto &w) { return w.isTwoSided(); }) == (independent ? 0 : 1),
                    "Paste honors the connection choice at placement");
            modes.undo(); require(modes.document() == source, "Connected and independent paste both undo atomically");
        }
    }

    {
        MapEditor indexed;
        indexed.resize(1000, 800);
        indexed.show();
        auto fixture = source;
        fixture.addSprite({256,256});
        indexed.recoverDocument(fixture, {});
        QApplication::processEvents();
        std::vector<std::pair<QGraphicsItem *, QSizeF>> fixedSizes;
        for (auto *item : indexed.scene()->items()) {
            require(!item->flags().testFlag(QGraphicsItem::ItemIgnoresTransformations),
                    "Map items participate in the scene spatial index");
            if (item->scale() != 1.0) {
                fixedSizes.push_back({item, item->deviceTransform(indexed.viewportTransform())
                                              .mapRect(item->boundingRect()).size()});
            }
        }
        require(fixedSizes.size() == fixture.walls().size() + fixture.vertices().size() + 2,
                "Wall markers, vertices, sprite and player start compensate for zoom");
        const auto checkSizesAndPicking = [&] {
            QApplication::processEvents();
            for (const auto &[item, size] : fixedSizes) {
                const auto actual = item->deviceTransform(indexed.viewportTransform())
                                        .mapRect(item->boundingRect()).size();
                require(std::abs(actual.width() - size.width()) < 0.001
                        && std::abs(actual.height() - size.height()) < 0.001,
                        "Screen-sized item bounds stay constant across zoom changes");
                if (item->isVisible()) {
                    const auto hits = indexed.scene()->items(item->sceneBoundingRect(),
                        Qt::IntersectsItemBoundingRect, Qt::DescendingOrder, indexed.viewportTransform());
                    require(hits.contains(item), "Spatial index finds resized screen-sized items");
                }
            }
        };
        for (qreal zoom : {25.0, 100.0, 400.0}) {
            indexed.setZoomPercent(zoom);
            checkSizesAndPicking();
        }
        const auto center = indexed.viewport()->rect().center();
        QWheelEvent wheel(QPointF(center), QPointF(indexed.viewport()->mapToGlobal(center)),
                          {}, {0,120}, Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(indexed.viewport(), &wheel);
        checkSizesAndPicking();
        // Rebuilding after an edit replaces the cached item list at the current zoom.
        indexed.recoverDocument(fixture, {});
        for (auto *item : indexed.scene()->items()) {
            if (item->scale() != 1.0) {
                require(std::abs(item->scale() * indexed.transform().m11() - 1.0) < 0.00001,
                        "Rebuilt items inherit the current inverse zoom");
            }
        }
    }
    {
        MapEditor thumbnails;
        thumbnails.setTextureResolver([](int tile, int) {
            QImage image(32, 32, QImage::Format_RGB32);
            image.fill(tile == 1 ? Qt::green : Qt::red);
            return image;
        });
        auto fixture = source;
        const auto spriteId = fixture.addSprite({256,256});
        fixture.setSpriteTexture(spriteId, 0);
        thumbnails.recoverDocument(fixture, {});
        const auto spriteItem = [&]() -> QGraphicsItem * {
            for (auto *item : thumbnails.scene()->items()) {
                if (item->data(Qt::UserRole + 3).isValid()) { return item; }
            }
            return nullptr;
        };
        const auto render = [&](int dpr) {
            QImage image(100*dpr, 100*dpr, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(dpr);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.translate(50,50);
            QStyleOptionGraphicsItem option;
            require(spriteItem(), "Sprite preview item exists");
            spriteItem()->paint(&painter, &option, nullptr);
            painter.end();
            return image;
        };
        for (int dpr : {1,2}) {
            thumbnails.setMode(MapEditor::Mode::Draw);
            const auto inactive = render(dpr);
            const auto gray = inactive.pixelColor(50*dpr,50*dpr);
            require(gray.red() == gray.green() && gray.green() == gray.blue(),
                    "Inactive cached sprite preview is grayscale");
            thumbnails.setMode(MapEditor::Mode::Sprites);
            const auto normal = render(dpr);
            require(normal.pixelColor(50*dpr,50*dpr) == QColor(Qt::red),
                    "Active cached sprite preview is colored at each display density");
            require(render(dpr) == normal, "Repeated cached sprite painting is stable");
            QGraphicsSceneHoverEvent enter(QEvent::GraphicsSceneHoverEnter);
            thumbnails.scene()->sendEvent(spriteItem(), &enter);
            require(render(dpr) != normal, "Cached sprite hover appearance updates");
            QGraphicsSceneHoverEvent leave(QEvent::GraphicsSceneHoverLeave);
            thumbnails.scene()->sendEvent(spriteItem(), &leave);
            spriteItem()->setSelected(true);
            require(render(dpr) != normal, "Cached sprite selection appearance updates");
            spriteItem()->setSelected(false);
            require(render(dpr) == normal, "Leaving hover and selection restores cached appearance");
        }
        spriteItem()->setSelected(true);
        thumbnails.setSelectedProperty(MapEditor::Property::Texture, 1);
        require(render(1).pixelColor(50,50) == QColor(Qt::green),
                "Texture changes use fresh cached artwork");
        const auto facingRight = render(1);
        thumbnails.setSelectedProperty(MapEditor::Property::Angle, 90);
        require(render(1) != facingRight, "Angle changes use the correct cached direction arrow");
    }
    {
        MapEditor vertices;
        vertices.recoverDocument(source, {});
        vertices.setMode(MapEditor::Mode::Vertices);
        QGraphicsItem *vertex = nullptr;
        for (auto *item : vertices.scene()->items()) {
            if (item->data(Qt::UserRole).isValid()) { vertex = item; break; }
        }
        require(vertex, "Vertex preview item exists");
        const auto render = [&](int dpr) {
            QImage image(20*dpr, 20*dpr, QImage::Format_ARGB32_Premultiplied);
            image.setDevicePixelRatio(dpr);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.translate(10,10);
            QStyleOptionGraphicsItem option;
            vertex->paint(&painter, &option, nullptr);
            painter.end();
            return image;
        };
        for (int dpr : {1,2}) {
            const auto normal = render(dpr);
            require(normal.pixelColor(10*dpr,10*dpr) == QColor(255,190,72),
                    "Cached vertex retains its normal color at each display density");
            QGraphicsSceneHoverEvent enter(QEvent::GraphicsSceneHoverEnter);
            vertices.scene()->sendEvent(vertex, &enter);
            const auto hovered = render(dpr);
            require(hovered.pixelColor(10*dpr,10*dpr) == QColor(80,210,255),
                    "Cached vertex hover color updates");
            require(hovered.pixelColor(14*dpr,10*dpr).alpha()
                    > normal.pixelColor(14*dpr,10*dpr).alpha(),
                    "Hovered vertex has a larger visible radius");
            vertex->setSelected(true);
            require(render(dpr).pixelColor(10*dpr,10*dpr) == QColor(255,110,92),
                    "Cached vertex selection overrides hover");
            vertex->setSelected(false);
            QGraphicsSceneHoverEvent leave(QEvent::GraphicsSceneHoverLeave);
            vertices.scene()->sendEvent(vertex, &leave);
            require(render(dpr) == normal, "Vertex returns to its normal cached appearance");
        }
    }
    editor.recoverDocument(source, {});
    editor.setMode(MapEditor::Mode::Draw);
    editor.setZoomPercent(200);
    editor.centerOn(768,512);
    QApplication::processEvents();

    // Panning must not place drawing points or change map history.
    const auto pan = [&](Qt::MouseButton button, bool space) {
        const QPoint start(400, 300), end(440, 330);
        const int horizontal = editor.horizontalScrollBar()->value();
        const int vertical = editor.verticalScrollBar()->value();
        if (space) {
            QKeyEvent key(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
            QApplication::sendEvent(&editor, &key);
        }
        QMouseEvent press(QEvent::MouseButtonPress, start, editor.viewport()->mapToGlobal(start),
                          button, button, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &press);
        if (space) {
            QKeyEvent key(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
            QApplication::sendEvent(&editor, &key);
        }
        QMouseEvent move(QEvent::MouseMove, end, editor.viewport()->mapToGlobal(end),
                         Qt::NoButton, button, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &move);
        require(editor.horizontalScrollBar()->value() == horizontal - 40
                && editor.verticalScrollBar()->value() == vertical - 30,
                "Both pan bindings move the viewport, even if Space is released first");
        QMouseEvent release(QEvent::MouseButtonRelease, end, editor.viewport()->mapToGlobal(end),
                            button, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &release);
        require(editor.cursor().shape() == Qt::CrossCursor, "Mouse release ends panning");
        require(editor.document() == source && editor.drawingPoints().empty()
                && editor.undoStack()->count() == 0, "Panning leaves geometry and history unchanged");
    };
    pan(Qt::LeftButton, true);
    pan(Qt::MiddleButton, false);
    QKeyEvent heldSpace(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(&editor, &heldSpace);
    QFocusEvent focusOut(QEvent::FocusOut);
    QApplication::sendEvent(&editor, &focusOut);
    editor.centerOn(768,512);

    click({1024,256});
    click({1536,256});
    click({1536,768});
    require(editor.document() == source && editor.drawingPoints().size() == 3,
            "Drawing preview does not change the existing wall");
    click({1024,768});
    require(editor.drawingPoints().empty() && editor.document().sectors().size() == 2,
            "Clicking back on the wall completes the room without retracing or Enter");
    require(editor.document().walls().size() == 9, "No overlapping closing wall");
    require(std::count_if(editor.document().walls().begin(), editor.document().walls().end(),
                         [](const auto &wall) { return wall.isTwoSided(); }) == 1,
            "The shared segment becomes a portal");
    require(editor.document().sectors()[1].floorz == -2048
            && editor.document().sectors()[1].ceilingz == -16384,
            "Attachment shares the source room's vertical opening");
    require(editor.undoStack()->count() == 1, "Attachment is one undo operation");
    const auto attached = editor.document();
    editor.undo();
    require(editor.document() == source, "Undo removes the room and both wall splits");
    editor.redo();
    require(editor.document() == attached, "Redo restores portal and room");

    QTemporaryDir directory;
    require(directory.isValid(), "Create temporary directory");
    QString error;
    const auto path = directory.filePath("attached.map");
    require(editor.saveMap(path, error), error.toUtf8().constData());
    MapDocument reopened;
    require(reopened.openMap(path, error), error.toUtf8().constData());
    require(reopened.sectors().size() == 2 && reopened.walls().size() == 9
            && std::count_if(reopened.walls().begin(), reopened.walls().end(),
                             [](const auto &wall) { return wall.isTwoSided(); }) == 1,
            "Saved map retains one shared portal rather than two solid walls");

    editor.undo();
    click({1024,256});
    click({1536,256});
    QKeyEvent cancel(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&editor, &cancel);
    require(editor.drawingPoints().empty() && editor.document() == source,
            "Cancelling an attachment leaves the source wall unchanged");

    MapDocument spriteMap;
    require(spriteMap.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true),
            "Create sprite-copy test room");
    auto firstSprite = MapDocument::Sprite{};
    firstSprite.position = {128, 128};
    firstSprite.z = -2048;
    firstSprite.texture = 1680;
    firstSprite.hitag = 17;
    firstSprite.lotag = 2;
    firstSprite.palette = 3;
    firstSprite.sectorId = 0;
    auto secondSprite = firstSprite;
    secondSprite.position = {384, 384};
    secondSprite.texture = 2000;
    secondSprite.lotag = 3;
    const auto firstId = spriteMap.addSprite(firstSprite.position);
    spriteMap.setSprite(firstId, firstSprite);
    const auto secondId = spriteMap.addSprite(secondSprite.position);
    spriteMap.setSprite(secondId, secondSprite);
    editor.recoverDocument(spriteMap, {});
    editor.setMode(MapEditor::Mode::Sprites);
    click(firstSprite.position);
    click(secondSprite.position, Qt::ShiftModifier);
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(&editor, &copy);
    QKeyEvent paste(QEvent::KeyPress, Qt::Key_V, Qt::ControlModifier);
    QApplication::sendEvent(&editor, &paste);
    require(editor.document().sprites().size() == 4,
            "Pasting duplicates all selected sprites");
    const auto &pastedFirst = editor.document().sprites()[2];
    const auto &pastedSecond = editor.document().sprites()[3];
    auto expectedFirst = firstSprite;
    auto expectedSecond = secondSprite;
    expectedFirst.position += QPointF(256, 256);
    expectedSecond.position += QPointF(256, 256);
    require(pastedFirst == expectedFirst && pastedSecond == expectedSecond,
            "Pasted sprites preserve their spacing and offset by one grid cell");
    editor.undo();
    require(editor.document().sprites().size() == 2, "Undo removes the pasted sprites together");
    editor.redo();
    require(editor.document().sprites().size() == 4, "Redo restores the pasted sprites");

    MapDocument raisedFloorMap;
    require(raisedFloorMap.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true),
            "Create raised-floor sprite test room");
    auto raisedFloor = raisedFloorMap.sectors()[0];
    raisedFloor.floorz = -2048;
    raisedFloorMap.setSector(0, raisedFloor);
    editor.recoverDocument(raisedFloorMap, {});
    editor.setMode(MapEditor::Mode::Sprites);
    rightClick({512,512});
    require(editor.document().sprites().size() == 1
            && editor.document().sprites()[0].z == -2048
            && editor.document().sprites()[0].sectorId == 0,
            "New sprites inherit the flat floor height and sector at their placement point");

    raisedFloor.floorstat |= 2;
    raisedFloor.floorheinum = 256;
    raisedFloorMap.setSector(0, raisedFloor);
    editor.recoverDocument(raisedFloorMap, {});
    editor.setMode(MapEditor::Mode::Sprites);
    rightClick({512,512});
    require(editor.document().sprites().size() == 1
            && editor.document().sprites()[0].z == -1536,
            "New sprites follow the local height of a sloped floor");

    MapDocument stacked = source;
    require(stacked.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}},true,&error,
        std::set<MapDocument::SectorId>{}), "Create independent overlapping fixture");
    stacked.setSectorCeilingZ(1,-32768);
    stacked.setSectorFloorZ(1,-24576);
    editor.recoverDocument(stacked,{});
    editor.setMode(MapEditor::Mode::Draw);
    QString scopeStatus;
    editor.editingScopeChanged = [&](const QString &text) { scopeStatus = text; };
    editor.centerOn(512,512);
    QApplication::processEvents();
    const auto choose = [&](int index, const std::function<void()> &operation) {
        bool prompted = false;
        QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,[&] {
            auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
            if (!dialog) { return; }
            prompted = true;
            if (index < 0) { dialog->reject(); }
            else {
                require(index < dialog->comboBoxItems().size(), "Target choice exists");
                dialog->setTextValue(dialog->comboBoxItems()[index]);
                dialog->accept();
            }
        });
        timer.start(1);
        operation();
        timer.stop();
        require(prompted,"Ambiguous target prompts instead of guessing");
    };
    choose(-1,[&] { click({512,0}); });
    require(editor.document() == stacked && editor.drawingPoints().empty(), "Cancelled target chooser changes nothing");
    choose(0,[&] { click({512,0}); });
    click({512,1024});
    require(editor.document().sectors().size() == 3 && editor.document().sectors()[1] == stacked.sectors()[1],
        "Scoped UI drawing splits lower room only");
    for (auto w : stacked.sectors()[1].walls) { require(editor.document().walls()[w] == stacked.walls()[w], "UI drawing preserves upper wall"); }
    require(editor.undoStack()->count() == 1, "Scoped drawing and boundary splits form one undo command");
    const auto split = editor.document();
    editor.undo();
    require(editor.document() == stacked && scopeStatus == "1 editable sector(s)","Undo restores independent overlapping topology and scope");
    editor.redo();
    require(editor.document() == split && scopeStatus == "2 editable sector(s)","Redo restores only intended edit and both child sectors in scope");

    editor.recoverDocument(stacked,{});
    editor.setMode(MapEditor::Mode::Vertices);
    bool heightPrompted = false;
    QTimer heightTimer;
    QObject::connect(&heightTimer,&QTimer::timeout,[&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog || !dialog->windowTitle().startsWith("Editable height")) { return; }
        auto fields = dialog->findChildren<QSpinBox *>();
        require(fields.size() == 2,"Height range fields exist");
        fields[0]->setValue(-32768);
        fields[1]->setValue(-24576);
        heightPrompted = true;
        dialog->accept();
    });
    heightTimer.start(1);
    editor.filterEditingHeight();
    heightTimer.stop();
    require(heightPrompted && editor.document() == stacked && editor.undoStack()->count() == 0,
        "Height filtering is view state, not a map edit");
    click({0,0});
    require(editor.scene()->selectedItems().size() == 1,"Scope makes coincident vertex selection unambiguous");
    const auto drag = [&](QPointF from, QPointF to) {
        const QPoint start = editor.mapFromScene(from), end = editor.mapFromScene(to);
        QMouseEvent press(QEvent::MouseButtonPress,start,editor.viewport()->mapToGlobal(start),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(),&press);
        QMouseEvent move(QEvent::MouseMove,end,editor.viewport()->mapToGlobal(end),Qt::NoButton,Qt::RightButton,Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(),&move);
        QMouseEvent release(QEvent::MouseButtonRelease,end,editor.viewport()->mapToGlobal(end),Qt::RightButton,Qt::NoButton,Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(),&release);
    };
    drag({0,0},{-256,0});
    require(editor.document().sectors()[0] == stacked.sectors()[0], "Dragging upper geometry keeps lower boundary");
    for (auto v : stacked.sectors()[0].vertices) { require(editor.document().vertices()[v] == stacked.vertices()[v],"Dragging upper vertex leaves coincident lower vertex unchanged"); }
    require(!(editor.document() == stacked),"Intended upper vertex moved");
    editor.undo();
    require(editor.document() == stacked,"Vertex move undo restores map");
    require(editor.setEditingScope(std::set<MapDocument::SectorId>{1}),"Restore upper editing scope");
    click({0,0});
    drag({0,0},{1024,1024});
    require(editor.document() == stacked,"Invalid collapsing drag is rolled back instead of deleting geometry");
    editor.recoverDocument(stacked,{});
    editor.setMode(MapEditor::Mode::Sprites);
    choose(1,[&] { rightClick({768,768}); });
    require(editor.document().sprites().size() == 1 && editor.document().sprites()[0].sectorId == 1
        && editor.document().sprites()[0].z == -24576,"Sprite placement resolves overlapping sector and inherits its floor");
    // Isolation controls picking, not explicit TROR constraints.
    auto tror = source;
    require(tror.extendTror(0,true,8192,error).has_value(), "Create linked fixture");
    editor.recoverDocument(tror,{});
    editor.setMode(MapEditor::Mode::Vertices);
    require(editor.setEditingScope(std::set<MapDocument::SectorId>{0}), "Isolate linked upper room");
    editor.centerOn(512,512);
    click({0,0}); drag({0,0},{-256,0});
    for (const auto &s : editor.document().sectors()) {
        require(editor.document().vertices()[s.vertices[0]].position == QPointF(-256,0), "Drag follows link into hidden layer");
    }
    require(editor.undoStack()->index() == 1, "Linked drag is one undo action");
    editor.undo();
    require(editor.document() == tror && editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({0}), "Undo restores geometry, links and scope");
    const auto splitPoint = editor.mapFromScene(QPointF(512,0));
    QMouseEvent doubleClick(QEvent::MouseButtonDblClick,splitPoint,editor.viewport()->mapToGlobal(splitPoint),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QApplication::sendEvent(editor.viewport(),&doubleClick);
    require(editor.document().walls().size() == tror.walls().size()+2, "Double click splits linked wall pair");
    require(editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({0}), "Linked split preserves isolation");
    editor.undo(); require(editor.document() == tror, "Linked split undo restores all boundaries");
    editor.goTrorLayer(true);
    require(editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({1}), "Navigate to lower layer");
    editor.goTrorLayer(false);
    require(editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({0})
        && editor.document() == tror && editor.undoStack()->index() == 0, "Layer navigation changes no map history");
    bool extensionPrompted = false;
    QTimer extensionTimer;
    QObject::connect(&extensionTimer,&QTimer::timeout,&editor,[&] {
        if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
            extensionPrompted = true; dialog->setIntValue(8192); dialog->accept();
        }
    });
    extensionTimer.start(1); editor.extendSelectedTror(false); extensionTimer.stop();
    require(extensionPrompted && editor.document().sectors().size() == 3
        && editor.editingScope() == std::optional<std::set<MapDocument::SectorId>>({2}), "Extension command creates and isolates new layer");
    const auto extended = editor.document();
    editor.undo(); require(editor.document() == tror, "Extension undo restores map");
    editor.redo(); require(editor.document() == extended, "Extension redo restores bunch and scope");
    bool disconnected = false;
    QTimer disconnectTimer;
    QObject::connect(&disconnectTimer,&QTimer::timeout,&editor,[&] {
        if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
            disconnected = true; dialog->button(QMessageBox::Yes)->click();
        }
    });
    disconnectTimer.start(1); editor.disconnectSelectedTror(true); disconnectTimer.stop();
    require(disconnected && !editor.document().sectors()[2].floorBunch, "Disconnect command clears selected whole bunch");
    editor.undo(); require(editor.document() == extended, "Disconnection undo restores links");
    editor.editingScopeChanged = {};

    // Keyboard/toolbar movement shares geometry validation and one undo transaction.
    editor.setGridSize(256);
    // The minimal Qt platform has no system cursor; commands use toolbar placement.
    const auto hover = [&](QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        const QPoint local = editor.mapFromScene(point);
        QMouseEvent event(QEvent::MouseMove, local, editor.viewport()->mapToGlobal(local),
                          Qt::NoButton, Qt::NoButton, modifiers);
        QApplication::sendEvent(editor.viewport(), &event);
    };
    const auto key = [&](int code) {
        QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
        QApplication::sendEvent(&editor, &event);
    };
    {
        MapDocument fixture;
        require(fixture.addPolyline({{0,0},{1024,0},{1024,1024},{0,1024}}, true), "Axis fixture");
        fixture.setPlayerStartPosition({768,768});
        const auto sprite = fixture.addSprite({256,256});
        fixture.setSpriteTexture(sprite, 42);
        for (bool drag : {false, true}) {
            for (int kind = 0; kind < 5; ++kind) {
                const auto mode = kind == 0 ? MapEditor::Mode::Vertices : kind == 1 ? MapEditor::Mode::Lines
                    : kind == 2 ? MapEditor::Mode::Sectors : MapEditor::Mode::Sprites;
                const QPointF grab = kind == 0 ? QPointF(0,0) : kind == 1 ? QPointF(512,0)
                    : kind == 2 ? QPointF(512,512) : kind == 3 ? QPointF(256,256) : QPointF(768,768);
                const auto position = [&]() {
                    return kind < 3 ? editor.document().vertices()[0].position
                        : kind == 3 ? editor.document().sprites()[0].position : editor.document().playerStart().position;
                };
                editor.recoverDocument(fixture, {});
                editor.clearEditingScope();
                editor.setMode(mode);
                editor.centerOn(512,512);
                click(grab);
                const auto original = position();
                if (drag) {
                    const auto local = editor.mapFromScene(grab);
                    QMouseEvent press(QEvent::MouseButtonPress, local, editor.viewport()->mapToGlobal(local),
                        Qt::RightButton, Qt::RightButton, Qt::NoModifier);
                    QApplication::sendEvent(editor.viewport(), &press);
                } else editor.moveSelection();
                hover(grab);
                key(Qt::Key_X);
                hover(grab + QPointF(-256,-256));
                require(position() == original + QPointF(-256,0), "Axis X fixes Y for every movable object");
                key(Qt::Key_Y);
                require(position() == original + QPointF(0,-256), "Switch axis recomputes from original without drift");
                key(Qt::Key_Y);
                const auto freeDelta = drag && kind != 0
                    ? editor.mapToScene(editor.mapFromScene(grab + QPointF(-256,-256)))
                        - editor.mapToScene(editor.mapFromScene(grab)) : QPointF(-256,-256);
                require(position() == original + freeDelta, "Repeat axis key restores free movement");
                key(Qt::Key_X);
                if (drag) {
                    const auto local = editor.mapFromScene(grab + QPointF(-256,-256));
                    QMouseEvent release(QEvent::MouseButtonRelease, local, editor.viewport()->mapToGlobal(local),
                        Qt::RightButton, Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(editor.viewport(), &release);
                } else key(Qt::Key_Return);
                require(position() == original + QPointF(-256,0) && editor.undoStack()->count() == 1,
                    "Constrained movement commits one undo transaction");
                const auto moved = editor.document();
                editor.undo(); require(editor.document() == fixture, "Undo constrained movement");
                editor.redo(); require(editor.document() == moved, "Redo constrained movement");
            }
        }
        // Right-drag must reclaim keyboard focus from a property control.
        // Deliver axis keys to the actual focused widget, unlike the direct
        // editor events above, and exercise multiple selected lines/vertices.
        for (auto mode : {MapEditor::Mode::Vertices, MapEditor::Mode::Lines}) {
            editor.recoverDocument(fixture, {});
            editor.setMode(mode);
            editor.centerOn(512,512);
            const auto grab = mode == MapEditor::Mode::Vertices ? QPointF(0,0) : QPointF(512,0);
            click(grab);
            click(mode == MapEditor::Mode::Vertices ? QPointF(1024,0) : QPointF(512,1024), Qt::ShiftModifier);
            require(editor.scene()->selectedItems().size() == 2, "Select multiple objects for focused right drag");
            QDoubleSpinBox property(editor.viewport());
            property.move(0,0); property.show();
            QApplication::processEvents();
            // This test sends input directly under the minimal platform plugin.
            // Set Qt's active widget window explicitly: native activateWindow()
            // is unreliable on headless Windows runners. The deprecated API is
            // intentional here; its replacement requests native activation.
            // The focus transfer under test still happens through the right drag.
            QT_WARNING_PUSH
            QT_WARNING_DISABLE_DEPRECATED
            QApplication::setActiveWindow(&editor);
            QT_WARNING_POP
            property.setFocus();
            QApplication::processEvents();
            require(property.hasFocus() || property.isAncestorOf(QApplication::focusWidget()), "Property control initially owns focus");
            const auto local = editor.mapFromScene(grab);
            QMouseEvent press(QEvent::MouseButtonPress, local, editor.viewport()->mapToGlobal(local),
                Qt::RightButton, Qt::RightButton, Qt::NoModifier);
            QApplication::sendEvent(editor.viewport(), &press);
            require(editor.hasFocus(), "Right drag transfers keyboard focus to editor");
            QKeyEvent x(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier);
            QApplication::sendEvent(QApplication::focusWidget(), &x);
            hover(grab + QPointF(-256,-256));
            require(editor.document().vertices()[0].position == QPointF(-256,0)
                && editor.document().vertices()[1].position == QPointF(768,0),
                "Focused right-drag X constrains all selected endpoints");
            QKeyEvent y(QEvent::KeyPress, Qt::Key_Y, Qt::AltModifier);
            QApplication::sendEvent(QApplication::focusWidget(), &y);
            hover(grab + QPointF(-128,-128), Qt::AltModifier);
            require(editor.document().vertices()[0].position.x() == 0
                && editor.document().vertices()[1].position.x() == 1024,
                "Switching axis while Alt is held preserves fixed coordinate");
            QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
            QApplication::sendEvent(QApplication::focusWidget(), &escape);
            require(editor.document() == fixture && editor.undoStack()->count() == 0,
                "Escape cancels constrained right-drag of multiple objects");
        }
        editor.recoverDocument(fixture, {});
        editor.setMode(MapEditor::Mode::Vertices);
        click({0,0}); click({1024,0}, Qt::ShiftModifier);
        gridScene->setGridAngle(30);
        editor.moveSelection(); hover({512,0}); key(Qt::Key_X); hover({212,173});
        const auto delta = editor.document().vertices()[0].position;
        require(delta.y() == 0 && editor.document().vertices()[1].position == QPointF(1024,0) + delta,
            "Rotated-grid snapping keeps locked coordinate and selection spacing");
        const auto grid = gridScene->toGrid(delta);
        require(std::abs(grid.x() / 256 - std::round(grid.x() / 256)) < 0.00001,
            "Constrained point lies on rotated grid line");
        hover({212,173}, Qt::AltModifier);
        const auto free = editor.mapToScene(editor.mapFromScene({212,173})).x() - 512;
        require(std::abs(editor.document().vertices()[0].position.x() - free) < 0.00001
            && editor.document().vertices()[0].position.y() == 0, "Alt disables snapping without unlocking axis");
        key(Qt::Key_Escape);
        require(editor.document() == fixture && editor.undoStack()->count() == 0, "Cancel constrained movement restores originals");
        gridScene->setGridAngle(0);
        editor.moveSelection(); hover({512,0}); hover({256,-256}); key(Qt::Key_Return);
        require(editor.document().vertices()[0].position == QPointF(-256,-256), "Next move starts unconstrained");

        // Pivot preview and acceptance share the same constrained coordinate.
        for (bool mirror : {false, true}) {
            editor.recoverDocument(fixture, {}); editor.setMode(MapEditor::Mode::Sectors); click({512,512});
            if (mirror) editor.mirrorSelection(true); else editor.rotateSelection();
            for (auto *button : editor.findChildren<QPushButton *>())
                if (button->isVisible() && button->text() == "Pick pivot in map") button->click();
            hover({512,512}); key(Qt::Key_X); hover({256,768});
            QDoubleSpinBox *x = nullptr, *y = nullptr;
            for (auto *box : editor.findChildren<QDoubleSpinBox *>("transformPivotX")) if (box->isVisible()) x = box;
            for (auto *box : editor.findChildren<QDoubleSpinBox *>("transformPivotY")) if (box->isVisible()) y = box;
            require(x && y && x->value() == 256 && y->value() == 512, "Constrained pivot previews along selection-center axis");
            key(Qt::Key_Y);
            require(x->value() == 512 && y->value() == 768, "Pivot axis switching uses original pivot");
            click({256,768});
            require(x->value() == 512 && y->value() == 768, "Pivot click accepts constrained preview");
            key(Qt::Key_Escape);
            require(editor.document() == fixture, "Pivot cancellation leaves geometry unchanged");
        }
        editor.recoverDocument(fixture, {}); editor.setMode(MapEditor::Mode::Sectors); click({512,512});
        for (int code : {Qt::Key_C, Qt::Key_V}) {
            QKeyEvent event(QEvent::KeyPress, code, Qt::ControlModifier);
            QApplication::sendEvent(&editor, &event);
        }
        require(editor.hasFloatingPaste(), "Axis floating paste exists");
        const auto pasteOrigin = editor.scene()->selectedItems().front()->pos();
        editor.moveSelection(); hover({512,512}); key(Qt::Key_X); hover({2048,1024});
        require(editor.scene()->selectedItems().front()->pos().y() == pasteOrigin.y(), "Floating paste movement fixes Y");
        key(Qt::Key_Y);
        require(editor.scene()->selectedItems().front()->pos().x() == pasteOrigin.x(), "Floating paste axis switch restores X");
        key(Qt::Key_Escape);
        require(!editor.hasFloatingPaste() && editor.document() == fixture, "Cancel constrained paste is atomic");
        editor.newMap(); editor.setDrawTool(MapEditor::DrawTool::Freeform); editor.centerOn(512,512);
        click({0,0}); key(Qt::Key_X); hover({512,256}); click({512,256});
        require(editor.drawingPoints().back() == QPointF(512,0), "Draw segment constrained to X");
        key(Qt::Key_Y); click({768,512});
        require(editor.drawingPoints().back() == QPointF(512,512), "Next segment uses its own origin");
        click({768,768});
        require(editor.drawingPoints().back() == QPointF(768,768), "Drawing resets constraint after each point");
        key(Qt::Key_Escape);
        require(editor.drawingPoints().empty() && editor.document().sectors().empty(), "Cancel constrained drawing");
    }
    {
        MapDocument original;
        require(original.addPolyline({{0,0},{1024,0},{1024,512},{0,512}}, true), "Transform UI source");
        original.setPlayerStartPosition({256,256});
        const auto sprite = original.addSprite({128,128});
        original.setSpriteAngle(sprite, 30);
        editor.recoverDocument(original, {});
        editor.clearEditingScope();
        editor.setMode(MapEditor::Mode::Sectors);
        editor.centerOn(512,256);
        click({512,256});
        const auto angleBox = [&]() -> QDoubleSpinBox * {
            for (auto *box : editor.findChildren<QDoubleSpinBox *>("transformAngle"))
                if (box->isVisible()) return box;
            require(false, "Transform angle control exists");
            return nullptr;
        };
        editor.rotateSelection();
        angleBox()->setValue(90);
        require(editor.document() == original && editor.undoStack()->count() == 0,
                "Rotation preview leaves document and history untouched");
        key(Qt::Key_Escape);
        require(editor.document() == original && editor.scene()->selectedItems().size() == 1,
                "Rotation cancellation preserves selection and geometry");
        editor.rotateSelection();
        angleBox()->setValue(90);
        key(Qt::Key_Return);
        const auto rotated = editor.document();
        require(!(rotated == original) && editor.undoStack()->count() == 1,
                "Exact rotation commits one undo operation");
        require(rotated.sprites()[0].angle == 120 && rotated.playerStart().angle == 90,
                "Sector rotation includes directional objects");
        editor.undo(); require(editor.document() == original, "Rotation undo restores all properties");
        editor.redo(); require(editor.document() == rotated, "Rotation redo restores all properties");
        editor.undo();
        editor.mirrorSelection(true);
        key(Qt::Key_Return);
        require(editor.document().sprites()[0].position == QPointF(896,128)
                && editor.document().sprites()[0].angle == 150, "Horizontal mirror flips X and facing");
        editor.undo(); require(editor.document() == original, "Mirror is undoable");
        editor.rotateSelection();
        for (auto *button : editor.findChildren<QPushButton *>())
            if (button->isVisible() && button->text() == "Pick pivot in map") button->click();
        click({0,0});
        angleBox()->setValue(90);
        key(Qt::Key_Return);
        require(editor.document().vertices()[1].position == QPointF(0,1024),
                "Custom pivot rotation uses picked origin");
        editor.undo();
        editor.rotateSelection();
        for (auto *box : editor.findChildren<QDoubleSpinBox *>("transformPivotX"))
            if (box->isVisible()) box->setValue(131072);
        angleBox()->setValue(180);
        key(Qt::Key_Return);
        require(editor.document() == original && angleBox()->isVisible(),
                "Invalid preview cannot commit and remains adjustable");
        key(Qt::Key_Escape);
        editor.rotateSelection();
        hover({1024,256}); hover({512,768});
        require(angleBox()->value() == 90, "Pointer rotation uses pivot-relative angle");
        key(Qt::Key_Escape);
        editor.rotateSelection();
        angleBox()->setValue(90);
        editor.setMode(MapEditor::Mode::Lines);
        require(editor.document() == original,
                "Mode switch cancels preview");
        editor.setMode(MapEditor::Mode::Sectors);
        click({512,256});
        editor.copySelectedSectors(); editor.pasteCopiedSectors();
        require(editor.hasFloatingPaste(), "Transform floating paste source");
        editor.rotateSelection(); angleBox()->setValue(90); key(Qt::Key_Return);
        require(editor.hasFloatingPaste() && editor.document() == original,
                "Transforming a floating paste keeps placement pending");
        key(Qt::Key_Escape);
        require(!editor.hasFloatingPaste() && editor.document() == original,
                "Cancelling transformed paste leaves source intact");
    }
    for (auto mode : {MapEditor::Mode::Vertices, MapEditor::Mode::Lines, MapEditor::Mode::Sectors}) {
        editor.recoverDocument(source, {});
        editor.clearEditingScope();
        editor.setMode(mode);
        editor.centerOn(512,512);
        const QPointF grab = mode == MapEditor::Mode::Vertices ? QPointF(0,0)
            : mode == MapEditor::Mode::Lines ? QPointF(512,0) : QPointF(512,512);
        click(grab);
        require(editor.scene()->selectedItems().size() == 1, "Select object for keyboard move");
        editor.moveSelection();
        hover(grab);
        require(!editor.canAutosave(), "Move preview is an open transaction");
        hover(grab + QPointF(-256,0));
        require(editor.document().vertices()[0].position == QPointF(-256,0), "Keyboard move snaps geometry");
        key(Qt::Key_Escape);
        require(editor.document() == source && editor.undoStack()->count() == 0
            && editor.scene()->selectedItems().size() == 1 && editor.canAutosave(),
            "Escape restores document and selection without undo history");
        editor.moveSelection();
        hover(grab);
        hover(grab + QPointF(-256,0));
        click(grab + QPointF(-256,0));
        const auto moved = editor.document();
        require(!(moved == source) && editor.undoStack()->count() == 1 && editor.canAutosave(),
            "Click commits one keyboard move");
        if (mode == MapEditor::Mode::Lines)
            require(moved.vertices()[1].position == QPointF(768,0)
                && moved.vertices()[2] == source.vertices()[2], "Line move translates both endpoints only");
        if (mode == MapEditor::Mode::Sectors)
            for (std::size_t v = 0; v < source.vertices().size(); ++v)
                require(moved.vertices()[v].position == source.vertices()[v].position + QPointF(-256,0),
                    "Sector keyboard move translates all boundary vertices");
        editor.undo(); require(editor.document() == source, "Keyboard move undo");
        editor.redo(); require(editor.document() == moved, "Keyboard move redo");
    }

    editor.recoverDocument(source, {});
    editor.setMode(MapEditor::Mode::Vertices);
    editor.centerOn(512,512);
    click({0,0}); click({1024,0}, Qt::ShiftModifier);
    // Toolbar movement starts at the center of the selected vertices.
    const QPointF selectionCenter(512,0);
    editor.moveSelection();
    require(editor.document() == source, "Starting toolbar move leaves the selection in place");
    hover(selectionCenter);
    require(editor.document() == source, "Centered pointer leaves the selection in place");
    hover(selectionCenter + QPointF(-100,0), Qt::AltModifier);
    const auto freeDelta = editor.mapToScene(editor.mapFromScene(selectionCenter + QPointF(-100,0)))
        - selectionCenter;
    require(editor.document().vertices()[0].position == freeDelta
        && editor.document().vertices()[1].position == QPointF(1024,0) + freeDelta,
        "Alt moves multiple selected vertices freely while preserving spacing");
    key(Qt::Key_Escape);
    click({0,0});
    editor.moveSelection(); hover({0,0}); hover({1024,1024}); key(Qt::Key_Return);
    require(editor.document() == source && editor.undoStack()->count() == 0,
        "Invalid keyboard move rolls back without history");
    editor.moveSelection(); key(Qt::Key_Return);
    require(editor.document() == source && editor.undoStack()->count() == 0,
        "Confirming without moving creates no history");

    editor.recoverDocument(source, {});
    editor.setMode(MapEditor::Mode::Sprites);
    editor.centerOn(512,512);
    const auto checkEmptySpritePreview = [&] {
        require(editor.document().sprites().size() == 1
            && editor.document().sprites()[0].texture == -1
            && editor.document().sprites()[0].position == QPointF(256,256)
            && editor.document().sprites()[0].sectorId == 0
            && editor.document().sprites()[0].z == -2048,
            "Empty sprite exists at its final placement before texture picker opens");
        const auto selected = editor.scene()->selectedItems();
        require(selected.size() == 1 && selected.front()->isVisible()
            && selected.front()->pos() == QPointF(256,256),
            "Empty sprite preview is visible and selected before texture picker opens");
        require(!editor.canAutosave() && editor.undoStack()->count() == 0,
            "Sprite preview remains pending until texture choice");
    };
    editor.setTextureSelector([&](std::optional<int>) -> std::optional<MapEditor::SpriteTexture> {
        checkEmptySpritePreview();
        return std::nullopt;
    });
    editor.addSprite(); click({256,256});
    require(editor.document() == source && editor.undoStack()->count() == 0,
        "Cancel add texture picker creates no sprite or history");
    editor.setTextureSelector([&](std::optional<int>) -> std::optional<MapEditor::SpriteTexture> {
        checkEmptySpritePreview();
        return MapEditor::SpriteTexture{42, QImage()};
    });
    editor.addSprite(); click({256,256});
    editor.setTextureSelector([](std::optional<int>) -> std::optional<MapEditor::SpriteTexture> {
        return MapEditor::SpriteTexture{42, QImage()};
    });
    require(editor.document().sprites().size() == 1 && editor.document().sprites()[0].texture == 42
        && editor.document().sprites()[0].position == QPointF(256,256)
        && editor.document().sprites()[0].sectorId == 0 && editor.document().sprites()[0].z == -2048
        && editor.undoStack()->count() == 1, "Add sprite sets texture, snapped position, sector and floor in one edit");
    const auto withSprite = editor.document();
    editor.moveSelection(); hover({256,256}); hover({512,256}); key(Qt::Key_Return);
    require(editor.document().sprites()[0].position == QPointF(512,256)
        && editor.undoStack()->count() == 2, "Enter commits sprite keyboard movement");
    editor.undo(); require(editor.document() == withSprite, "Undo sprite movement");
    click({512,512}); editor.moveSelection(); hover({512,512}); hover({768,512}); key(Qt::Key_Return);
    require(editor.document().playerStart().position == QPointF(768,512), "Keyboard move supports player start");
    editor.undo();

    // A toolbar invocation starts outside the viewport and waits for a placement click.
    QCursor::setPos(editor.viewport()->mapToGlobal(QPoint(-30,-30)));
    editor.addSprite();
    require(editor.document() == withSprite, "Toolbar add waits for a position");
    key(Qt::Key_Escape); click({768,768});
    require(editor.document() == withSprite, "Escape cancels pending sprite placement");
    QCursor::setPos(editor.viewport()->mapToGlobal(QPoint(-30,-30)));
    editor.addSprite(); click({768,768});
    require(editor.document().sprites().size() == 2, "Toolbar add places sprite at clicked position");
    editor.undo(); require(editor.document() == withSprite, "Toolbar add undo");
    click({256,256});
    QCursor::setPos(editor.viewport()->mapToGlobal(QPoint(-30,-30)));
    editor.moveSelection(); hover({256,256});
    require(editor.document() == withSprite, "Toolbar move establishes origin on entering the map");
    hover({512,256});
    QFocusEvent lostFocus(QEvent::FocusOut);
    QApplication::sendEvent(&editor, &lostFocus);
    require(editor.document() == withSprite && editor.canAutosave(), "Losing focus cancels movement safely");

    // Property edits and surface alignment participate in the normal undo stack.
    editor.recoverDocument(withSprite, {});
    editor.setMode(MapEditor::Mode::Sprites);
    click({256,256});
    editor.setSelectedProperty(MapEditor::Property::Alignment, 2);
    editor.setSelectedProperty(MapEditor::Property::SpriteSlope, -4096);
    require(editor.document().sprites()[0].slope() == -4096, "Sprite slope property edits packed representation");
    editor.undo();
    require(editor.document().sprites()[0].slope() == 0, "Undo slope property");
    editor.redo();
    require(editor.document().sprites()[0].slope() == -4096, "Redo slope property");
    const auto beforeAlign = editor.document();
    editor.alignSelectedSpritesToSurface(true);
    require(editor.document().sprites()[0].slope() == 0
            && editor.document().sprites()[0].z == editor.document().sectors()[0].floorz,
            "Alignment action matches flat floor");
    editor.undo();
    require(editor.document() == beforeAlign, "Undo alignment restores slope and height");

    // Enter edits a selected sprite, but never the player start or an empty selection.
    editor.recoverDocument(withSprite, {});
    editor.setMode(MapEditor::Mode::Sprites);
    click({256,256});
    int pickerCalls = 0;
    editor.setTextureSelector([&](std::optional<int> current) -> std::optional<MapEditor::SpriteTexture> {
        ++pickerCalls;
        require(current == 42, "Enter picker starts at the current sprite texture");
        return MapEditor::SpriteTexture{43, QImage()};
    });
    key(Qt::Key_Return);
    auto textured = withSprite;
    textured.setSpriteTexture(0, 43);
    require(editor.document() == textured && pickerCalls == 1
        && editor.undoStack()->count() == 1 && editor.scene()->selectedItems().size() == 1,
        "Enter changes only sprite texture in one undoable edit and retains selection");
    editor.undo(); require(editor.document() == withSprite, "Undo Enter texture change");
    editor.redo(); require(editor.document() == textured, "Redo Enter texture change");
    editor.setTextureSelector([&](std::optional<int>) -> std::optional<MapEditor::SpriteTexture> {
        ++pickerCalls;
        return std::nullopt;
    });
    key(Qt::Key_Enter);
    require(pickerCalls == 2 && editor.document() == textured && editor.undoStack()->count() == 1,
        "Keypad Enter opens picker; cancellation changes neither map nor history");
    QKeyEvent repeatedEnter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier, QString(), true);
    QApplication::sendEvent(&editor, &repeatedEnter);
    require(pickerCalls == 2, "Held Enter does not reopen picker");
    click({512,512}); key(Qt::Key_Return);
    editor.scene()->clearSelection(); key(Qt::Key_Return);
    require(pickerCalls == 2 && editor.document() == textured,
        "Enter ignores player start and empty selection");
    click({256,256}); editor.moveSelection(); hover({256,256}); hover({512,256}); key(Qt::Key_Return);
    require(pickerCalls == 2 && editor.document().sprites()[0].position == QPointF(512,256),
        "Enter confirms active move without opening texture picker");

    // Sector paste remains outside the document until the floating group is deselected.
    editor.recoverDocument(source, {});
    editor.setMode(MapEditor::Mode::Sectors);
    editor.centerOn(1024,512);
    QApplication::processEvents();
    click({512,512});
    const auto shortcut = [&](int code) {
        QKeyEvent event(QEvent::KeyPress, code, Qt::ControlModifier);
        QApplication::sendEvent(&editor, &event);
    };
    shortcut(Qt::Key_C);
    QCursor::setPos(editor.viewport()->mapToGlobal(editor.mapFromScene({2048,0})));
    shortcut(Qt::Key_V);
    require(editor.hasFloatingPaste() && editor.document() == source && editor.undoStack()->count() == 0,
        "Paste creates a floating group without changing geometry or history");
    require(!editor.canAutosave() && editor.hasUnsavedChanges(), "Pending paste participates in dirty and autosave state");
    const auto dragFloating = [&](QPointF from, QPointF to) {
        const QPoint a = editor.mapFromScene(from), b = editor.mapFromScene(to);
        QMouseEvent press(QEvent::MouseButtonPress, a, editor.viewport()->mapToGlobal(a),
            Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &press);
        QMouseEvent move(QEvent::MouseMove, b, editor.viewport()->mapToGlobal(b),
            Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &move);
        QMouseEvent release(QEvent::MouseButtonRelease, b, editor.viewport()->mapToGlobal(b),
            Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(editor.viewport(), &release);
    };
    require(editor.scene()->selectedItems().size() == 1, "Floating sectors are selected as one group");
    const auto floatingItem = editor.scene()->selectedItems().front();
    const auto floatingOffset = floatingItem->pos();
    const auto floatingCenter = floatingItem->sceneBoundingRect().center();
    dragFloating(floatingCenter, floatingCenter + QPointF(512,0));
    require(editor.hasFloatingPaste() && editor.document() == source && editor.undoStack()->count() == 0,
        "Releasing a floating-sector drag does not settle it");
    click({-512,-512});
    require(!editor.hasFloatingPaste() && editor.document().sectors().size() == 2
        && editor.undoStack()->count() == 1 && editor.undoStack()->undoText() == "Paste sectors",
        "Deselect settles the entire paste as one undo step");
    require(editor.document().vertices()[4].position == source.vertices()[0].position + floatingOffset + QPointF(512,0),
        "Floating movement snaps the group without moving its source");
    const auto pastedSectors = editor.document();
    editor.undo(); require(editor.document() == source, "Undo removes the entire sector paste");
    editor.redo(); require(editor.document() == pastedSectors, "Redo restores final placement");
    shortcut(Qt::Key_V); key(Qt::Key_Escape);
    require(!editor.hasFloatingPaste() && editor.document() == pastedSectors && editor.undoStack()->count() == 1,
        "Escape cancels a paste without creating history");
    shortcut(Qt::Key_V); editor.undo();
    require(!editor.hasFloatingPaste() && editor.document() == pastedSectors && editor.undoStack()->index() == 1,
        "Undo during floating paste cancels only the pending copy");
    shortcut(Qt::Key_V);
    editor.scene()->clearSelection();
    QApplication::processEvents();
    require(!editor.hasFloatingPaste() && editor.document().sectors().size() == 3,
        "Explicit selection clearing also settles the paste");
    editor.undo();
    shortcut(Qt::Key_V);
    editor.setMode(MapEditor::Mode::Lines);
    require(!editor.hasFloatingPaste() && editor.document().sectors().size() == 3,
        "Mode switch settles a floating paste");
    editor.undo();
    editor.setMode(MapEditor::Mode::Sectors);
    shortcut(Qt::Key_V);
    QTemporaryDir pasteDirectory;
    QString pasteError;
    require(editor.saveMap(pasteDirectory.filePath("paste.map"), pasteError), "Save settles and saves floating sectors");
    require(!editor.hasFloatingPaste() && !editor.hasUnsavedChanges(), "Saved paste is clean");
    MapDocument savedPaste;
    require(savedPaste.openMap(pasteDirectory.filePath("paste.map"), pasteError)
        && savedPaste.sectors().size() == 3, "Saved map contains settled paste");
    shortcut(Qt::Key_V);
    editor.newMap();
    require(!editor.hasFloatingPaste() && editor.document().sectors().empty(), "New map discards pending paste");

    MapDocument adjacent = source;
    require(adjacent.addPolyline({{1024,0},{2048,0},{2048,1024},{1024,1024}}, true), "Create multiselection fixture");
    const auto sectorSprite = adjacent.addSprite({256,256});
    adjacent.setSpriteTexture(sectorSprite, 123);
    editor.recoverDocument(adjacent, {});
    editor.setMode(MapEditor::Mode::Sectors);
    editor.centerOn(1024,512);
    click({512,512}); click({1536,512}, Qt::ShiftModifier);
    require(editor.scene()->selectedItems().size() == 2, "Select two sectors to copy");
    shortcut(Qt::Key_C); shortcut(Qt::Key_V);
    const auto beforeKeyboardMove = editor.scene()->selectedItems().front()->pos();
    editor.moveSelection();
    hover({2048,2048});
    hover({2560,2048});
    key(Qt::Key_Return);
    require(editor.hasFloatingPaste() && editor.document() == adjacent
        && editor.scene()->selectedItems().front()->pos() != beforeKeyboardMove,
        "Keyboard positioning leaves the copied group floating");
    const auto groupCenter = editor.scene()->selectedItems().front()->sceneBoundingRect().center();
    click(groupCenter, Qt::ShiftModifier);
    require(!editor.hasFloatingPaste() && editor.document().sectors().size() == 4
        && editor.document().sprites().size() == 2 && editor.undoStack()->count() == 1,
        "Shift-deselect settles all copied sectors and their sprites together");
    editor.undo(); require(editor.document() == adjacent, "Undo multisection paste restores source");
    editor.redo();
    require(editor.document().sectors().size() == 4, "Redo restores multisection paste");

    // A rejected commit must leave the pending fragment available for cancellation.
    MapDocument invalidSource = source;
    invalidSource.setVertexPositions({{1, invalidSource.vertices()[0].position}});
    editor.recoverDocument(invalidSource, {});
    editor.setMode(MapEditor::Mode::Sectors);
    click({256,768});
    shortcut(Qt::Key_C); shortcut(Qt::Key_V);
    require(editor.hasFloatingPaste(), "Invalid fixture can be previewed before placement validation");
    editor.scene()->clearSelection();
    QApplication::processEvents();
    require(editor.hasFloatingPaste() && editor.document() == invalidSource && editor.undoStack()->count() == 0,
        "Rejected placement stays floating and creates no undo entry");
    require(!editor.saveMap(pasteDirectory.filePath("invalid.map"), pasteError)
        && !pasteError.isEmpty() && editor.hasFloatingPaste(), "Save reports an invalid paste without discarding it");
    key(Qt::Key_Escape);

    // Reference images remain editor metadata through transforms and map I/O.
    {
        QTemporaryDir directory;
        const auto imagePath = directory.filePath("guide.png");
        QImage image(400,200,QImage::Format_ARGB32);
        image.fill(QColor(180,80,40));
        require(image.save(imagePath), "Write reference PNG fixture");
        editor.recoverDocument(source, {});
        editor.setMode(MapEditor::Mode::Vertices);
        editor.centerOn(512,512);
        QString error;
        require(editor.loadReferenceImage(imagePath,error), "Load reference PNG");
        require(editor.referenceImage() && editor.referenceImage()->locked
            && editor.document() == source && editor.hasUnsavedChanges(), "Image loads locked without changing map geometry");
        editor.undo(); require(!editor.referenceImage(), "Undo reference load");
        editor.redo(); require(editor.referenceImage().has_value(), "Redo reference load");
        editor.showReferenceImageControls();
        auto *dialog = editor.findChild<QDialog *>("referenceImageDialog");
        require(dialog, "Reference controls available");
        const auto number = [&](const char *name) {
            auto *box = dialog->findChild<QDoubleSpinBox *>(name);
            require(box, "Reference numeric control"); return box;
        };
        auto *lock = dialog->findChild<QCheckBox *>("referenceLocked");
        auto *visible = dialog->findChild<QCheckBox *>("referenceVisible");
        number("referenceX")->setValue(0); number("referenceY")->setValue(0);
        number("referenceWidth")->setValue(1024);
        require(editor.referenceImage()->size == QSizeF(1024,512), "Numeric resize preserves image aspect ratio");
        number("referenceRotation")->setValue(30);
        require(editor.referenceImage()->rotation == 30, "Reference rotation control");
        number("referenceRotation")->setValue(0);
        number("referenceOpacity")->setValue(35);
        require(std::abs(editor.referenceImage()->opacity-0.35) < 0.00001, "Reference opacity control");
        visible->setChecked(false); require(!editor.referenceImage()->visible, "Reference hide control");
        visible->setChecked(true);
        click({0,0});
        require(editor.scene()->selectedItems().size() == 1 && editor.document() == source,
            "Locked image permits selecting map geometry beneath it");
        lock->setChecked(false);
        dialog->hide();
        const auto before = *editor.referenceImage();
        const auto dragImage = [&](QPointF from, QPointF to, bool cancel) {
            auto local = editor.mapFromScene(from);
            QMouseEvent press(QEvent::MouseButtonPress, local, editor.viewport()->mapToGlobal(local),
                Qt::LeftButton, Qt::LeftButton, Qt::AltModifier);
            QApplication::sendEvent(editor.viewport(), &press);
            hover(to, Qt::AltModifier);
            if (cancel) key(Qt::Key_Escape);
            else {
                local = editor.mapFromScene(to);
                QMouseEvent release(QEvent::MouseButtonRelease, local, editor.viewport()->mapToGlobal(local),
                    Qt::LeftButton, Qt::NoButton, Qt::AltModifier);
                QApplication::sendEvent(editor.viewport(), &release);
            }
        };
        const auto count = editor.undoStack()->count();
        dragImage({512,256},{768,512},false);
        require(editor.referenceImage()->position != before.position && editor.document() == source
            && editor.undoStack()->count() == count+1, "Image drag is one undoable edit independent of geometry");
        editor.undo(); require(*editor.referenceImage() == before, "Undo image drag restores transform");
        dragImage({512,256},{768,512},true);
        require(*editor.referenceImage() == before, "Escape cancels image drag");
        dragImage({1024,512},{1536,768},false);
        require(editor.referenceImage()->size.width() > 1024 && editor.referenceImage()->position == QPointF(0,0)
            && std::abs(editor.referenceImage()->size.width()/editor.referenceImage()->size.height()-2) < 0.00001,
            "Corner resizing preserves aspect and opposite anchor");
        editor.undo();
        auto *calibrate = dialog->findChild<QPushButton *>("calibrateReferenceImage");
        calibrate->click();
        click({256,256});
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &editor, [&] {
            if (auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                input->setDoubleValue(1024); input->accept();
            }
        });
        timer.start(1); click({768,256}); timer.stop();
        require(std::abs(editor.referenceImage()->size.width()-2048) < 4
            && editor.document() == source, "Two-point calibration sets map scale without editing map geometry");
        editor.undo(); require(*editor.referenceImage() == before, "Undo calibration restores reference transform");
        lock->setChecked(true);
        const auto metadata = editor.referenceImageMetadata();
        const auto mapPath = directory.filePath("reference.map");
        require(editor.saveMap(mapPath,error), "Save map and reference sidecar");
        require(!editor.hasUnsavedChanges(), "Saved reference marks editor clean");
        QFile sidecar(mapPath + ".dukebuilder.json");
        require(sidecar.open(QIODevice::ReadOnly), "Reference sidecar exists");
        const auto json = QJsonDocument::fromJson(sidecar.readAll()).object(); sidecar.close();
        require(json["referenceImage"].toObject()["file"].toString() == "guide.png", "Sidecar uses relative image paths");
        MapDocument plain;
        require(plain.openMap(mapPath,error) && plain.sprites().size() == source.sprites().size(), "Build map contains no reference sprite");
        editor.newMap(); require(!editor.referenceImage(), "New map clears reference image");
        require(editor.openMap(mapPath,error) && editor.referenceImageMetadata() == metadata
            && !editor.referenceImage()->image.isNull() && !editor.hasUnsavedChanges(), "Reopen restores image, transform and clean state");
        require(!editor.loadReferenceImage(directory.filePath("missing.png"),error)
            && editor.referenceImageMetadata() == metadata, "Failed image replacement preserves reference");
        // Missing external images retain placement and emit a useful warning.
        require(QFile::rename(imagePath,directory.filePath("moved.png")), "Move image fixture");
        QString warning;
        editor.referenceImageWarning = [&](const QString &message) { warning = message; };
        require(editor.openMap(mapPath,error) && !warning.isEmpty() && editor.referenceImage()->image.isNull()
            && editor.referenceImageMetadata() == metadata, "Missing image preserves metadata and reports warning");
        require(editor.loadReferenceImage(directory.filePath("moved.png"),error)
            && editor.referenceImage()->size == before.size, "Relinking image preserves placement");
        editor.referenceImageWarning = {};
        dialog->findChild<QPushButton *>("removeReferenceImage")->click();
        require(!editor.referenceImage(), "Remove reference image");
        require(editor.saveMap(mapPath,error) && editor.openMap(mapPath,error) && !editor.referenceImage(),
            "Removed reference stays removed after reopen");
        dialog->close();
    }

}
