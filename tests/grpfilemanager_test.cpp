#include "grpfilemanagerwindow.h"
#include <libduke/grp.h>
#include <QApplication>
#include <QAction>
#include <QDataStream>
#include <QPlainTextEdit>
#include <QLabel>
#include <QListWidget>
#include <QSpinBox>
#include <QMenu>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <QScrollBar>
#include <QStatusBar>
#include <QUndoStack>
#include <QtPlugin>
#include <cstdlib>
#include <functional>
#include <iostream>

#ifdef DUKE_BUILDER_STATIC_MINIMAL_PLUGIN
Q_IMPORT_PLUGIN(QMinimalIntegrationPlugin)
#endif

static void require(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}

struct GrpFileManagerTest {
    static bool create(GrpFileManagerWindow &window, const QString &path) { return window.createArchive(path); }
    static bool append(GrpFileManagerWindow &window, const QStringList &paths) { return window.appendFiles(paths); }
    static bool save(GrpFileManagerWindow &window) { return window.saveArchive(); }
    static bool dirty(const GrpFileManagerWindow &window) { return window.m_dirty; }
    static bool load(GrpFileManagerWindow &window, const QString &path) { return window.loadArchive(path); }
    static bool replace(GrpFileManagerWindow &window, const QString &path) { return window.replaceSelected(path); }
    static void remove(GrpFileManagerWindow &window) { window.deleteSelected(); }
    static QUndoStack *history(GrpFileManagerWindow &window) { return window.m_history; }
    static QTreeWidget *list(GrpFileManagerWindow &window) { return window.m_files; }
    static bool extract(GrpFileManagerWindow &window, const std::vector<int> &rows, const QString &path) {
        return window.extractFiles(rows, path);
    }
};

static void write(const QString &path, const QByteArray &data)
{
    QFile file(path);
    require(file.open(QIODevice::WriteOnly) && file.write(data) == data.size(), "Write fixture");
}
static QByteArray read(const QString &path)
{
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Read fixture");
    return file.readAll();
}

struct Choice { QString action; bool all = false; };
static bool choices(const std::vector<Choice> &responses, const std::function<bool()> &operation)
{
    std::size_t index = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!dialog) return;
        require(index < responses.size(), "Unexpected conflict/error prompt");
        const auto response = responses[index++];
        require(dialog->checkBox(), "Conflict prompt offers apply to all");
        dialog->checkBox()->setChecked(response.all);
        for (auto *button : dialog->buttons()) {
            if (button->text() == response.action
                || (response.action == "Cancel" && dialog->standardButton(button) == QMessageBox::Cancel)) {
                button->click();
                return;
            }
        }
        require(false, "Expected conflict action is available");
    });
    timer.start(1);
    const bool result = operation();
    timer.stop();
    require(index == responses.size(), "Expected number of conflict prompts");
    return result;
}

static std::vector<std::pair<QString, QByteArray>> members(const QString &path)
{
    auto *grp = duke_grp_new();
    require(grp && duke_grp_open_filename(grp, QFile::encodeName(path).constData())
            && duke_grp_read_entries_full(grp), "Read saved archive");
    std::vector<std::pair<QString, QByteArray>> result;
    for (uint32_t i = 0; i < grp->header.entry_count; ++i) {
        auto *entry = duke_grp_get_entry_by_index(grp, i);
        void *data = nullptr;
        const auto size = duke_grp_get_file_data_by_index(grp, i, &data);
        require(entry && size != static_cast<size_t>(-1), "Read member data");
        result.emplace_back(QString::fromLocal8Bit(entry->filename),
                            QByteArray(static_cast<const char *>(data), qsizetype(size)));
    }
    duke_grp_free(grp);
    return result;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir directory;
    require(directory.isValid(), "Temporary directory");
    QDir root(directory.path());
    for (const auto *name : {"one", "two", "out", "cancel"}) require(root.mkdir(name), "Fixture directory");
    const auto path = [&](const QString &name) { return root.filePath(name); };
    for (const auto *name : {"A.TXT", "B.TXT", "E.TXT"}) {
        write(path("one/" + QString(name)), "original");
        write(path("two/" + QString(name)), "replacement");
    }
    write(path("two/C.TXT"), "new");
    write(path("two/D.TXT"), "cancelled");
    GrpFileManagerWindow window;
    auto *saveAction = window.findChild<QAction *>("grpSaveAction");
    auto *replaceAction = window.findChild<QAction *>("grpReplaceAction");
    auto *extractAction = window.findChild<QAction *>("grpExtractAction");
    auto *selectAllAction = window.findChild<QAction *>("grpSelectAllAction");
    require(saveAction && replaceAction && extractAction && selectAllAction, "Shared archive actions exist");
    require(!saveAction->isEnabled() && !replaceAction->isEnabled() && !extractAction->isEnabled(),
            "Archive actions disabled before opening archive");
    const auto archive = path("test.grp");
    require(GrpFileManagerTest::create(window, archive), "Create archive");
    require(GrpFileManagerTest::append(window, {path("one/A.TXT"), path("one/B.TXT")}), "Initial append");
    require(GrpFileManagerTest::save(window), "Initial save");
    require(choices({{"Skip", true}}, [&] {
        return GrpFileManagerTest::append(window, {path("two/A.TXT"), path("two/B.TXT"), path("two/C.TXT")});
    }), "Skip all conflicts still appends new names");
    require(GrpFileManagerTest::save(window), "Save skipped batch");
    auto saved = members(archive);
    require(saved.size() == 3 && saved[0].second == "original" && saved[1].second == "original"
            && saved[2].first == "C.TXT", "Skip preserves originals and order");
    require(choices({{"Replace", true}}, [&] {
        return GrpFileManagerTest::append(window, {path("two/A.TXT"), path("two/B.TXT")});
    }), "Replace all conflicts");
    require(GrpFileManagerTest::save(window), "Save replaced batch");
    saved = members(archive);
    require(saved.size() == 3 && saved[0].first == "A.TXT" && saved[1].first == "B.TXT"
            && saved[0].second == "replacement" && saved[1].second == "replacement", "Replace retains entry order");
    require(!choices({{"Replace"}, {"Cancel"}}, [&] {
        return GrpFileManagerTest::append(window, {path("one/A.TXT"), path("two/D.TXT"), path("one/B.TXT")});
    }), "Cancel append");
    require(!GrpFileManagerTest::dirty(window), "Cancelled append retains clean state");
    require(GrpFileManagerTest::save(window) && members(archive) == saved, "Cancel rolls back replacements and additions");
    require(choices({{"Skip", true}}, [&] {
        return GrpFileManagerTest::append(window, {path("one/A.TXT"), path("one/B.TXT")});
    }) && !GrpFileManagerTest::dirty(window), "All-skipped append does not dirty archive");
    require(choices({{"Replace"}}, [&] {
        return GrpFileManagerTest::append(window, {path("one/E.TXT"), path("two/E.TXT")});
    }), "Duplicates within one input batch use conflict choices");
    require(GrpFileManagerTest::save(window), "Save duplicate-input batch");
    saved = members(archive);
    require(saved.size() == 4 && saved[3].second == "replacement", "Last replacement wins within batch");

    write(path("out/A.TXT"), "keep");
    write(path("out/B.TXT"), "keep");
    require(choices({{"Skip", true}}, [&] { return GrpFileManagerTest::extract(window, {0,1,2,3}, path("out")); }),
            "Extraction skip all");
    require(read(path("out/A.TXT")) == "keep" && read(path("out/B.TXT")) == "keep"
            && read(path("out/C.TXT")) == "new" && read(path("out/E.TXT")) == "replacement", "Skip existing, extract new files");
    require(choices({{"Replace", true}}, [&] { return GrpFileManagerTest::extract(window, {0,1,2,3}, path("out")); }),
            "Extraction replace all");
    require(read(path("out/A.TXT")) == "replacement" && read(path("out/B.TXT")) == "replacement", "Confirmed overwrite succeeds");
    write(path("cancel/A.TXT"), "keep A");
    write(path("cancel/B.TXT"), "keep B");
    require(!choices({{"Replace"}, {"Cancel"}}, [&] {
        return GrpFileManagerTest::extract(window, {2,0,1}, path("cancel"));
    }), "Cancel extraction conflicts");
    require(!QFileInfo::exists(path("cancel/C.TXT")) && read(path("cancel/A.TXT")) == "keep A"
            && read(path("cancel/B.TXT")) == "keep B", "Cancelled preflight writes no files");
    require(!GrpFileManagerTest::dirty(window) && members(archive) == saved, "Extraction does not modify archive");
    auto *history = GrpFileManagerTest::history(window);
    auto *list = GrpFileManagerTest::list(window);
    selectAllAction->trigger();
    require(list->selectedItems().size() == 4 && extractAction->isEnabled() && !replaceAction->isEnabled(),
            "Select All enables batch extraction and disables single-file replacement");
    require(window.statusBar()->currentMessage().contains("4 selected"), "Selection count updates in status bar");
    list->clearSelection();
    require(!extractAction->isEnabled() && window.statusBar()->currentMessage().contains("0 selected"),
            "Clearing selection updates action state and summary");
    require(list->topLevelItem(3)->text(3).isEmpty(), "Save clears change markers");
    history->undo();
    require(list->topLevelItemCount() == 3 && GrpFileManagerTest::dirty(window)
            && window.statusBar()->currentMessage().contains("1 deleted"), "Undo across save marks missing saved entry as deleted");
    history->redo();
    require(!GrpFileManagerTest::dirty(window), "Redo to saved contents restores clean state");

    list->setCurrentItem(list->topLevelItem(0));
    require(GrpFileManagerTest::replace(window, path("one/A.TXT")), "Replace selected entry");
    require(list->topLevelItem(0)->text(3) == "Replaced" && list->currentItem()->text(0) == "A.TXT",
            "Replace marks entry and retains current selection");
    history->undo();
    require(!GrpFileManagerTest::dirty(window) && list->topLevelItem(0)->text(3).isEmpty(), "Undo replacement clears marker");
    history->redo();
    require(list->topLevelItem(0)->text(3) == "Replaced", "Redo restores replacement marker");
    history->undo();

    list->clearSelection();
    list->topLevelItem(0)->setSelected(true);
    list->topLevelItem(1)->setSelected(true);
    QTimer confirmation;
    QObject::connect(&confirmation, &QTimer::timeout, [&] {
        if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            dialog->button(QMessageBox::Yes)->click();
    });
    confirmation.start(1);
    const auto beforeDeleteIndex = history->index();
    GrpFileManagerTest::remove(window);
    confirmation.stop();
    require(list->topLevelItemCount() == 2 && history->index() == beforeDeleteIndex + 1
            && window.statusBar()->currentMessage().contains("2 deleted"), "Multi-file deletion is one undo step");
    history->undo();
    require(list->topLevelItemCount() == 4 && list->selectedItems().size() == 2
            && list->currentItem()->text(0) == "A.TXT" && !GrpFileManagerTest::dirty(window),
            "Undo deletion restores entries, selection and clean state");
    history->redo();
    require(list->topLevelItemCount() == 2, "Redo deletes the same entries");
    history->undo();

    QStringList many;
    for (int i = 0; i < 80; ++i) {
        const auto filename = path(QString("two/F%1.TXT").arg(i));
        write(filename, "data");
        many.push_back(filename);
    }
    require(GrpFileManagerTest::append(window, many), "Append many files as one batch");
    require(list->topLevelItem(4)->text(3) == "Added", "New entries marked Added");
    history->undo();
    require(list->topLevelItemCount() == 4 && !GrpFileManagerTest::dirty(window), "One undo removes whole append batch");
    history->redo();
    require(list->topLevelItemCount() == 84, "Redo restores complete append batch");
    window.show();
    QApplication::processEvents();
    list->clearSelection();
    list->setCurrentItem(list->topLevelItem(30));
    list->topLevelItem(32)->setSelected(true);
    list->setColumnWidth(0, 1600);
    list->doItemsLayout();
    list->verticalScrollBar()->setValue(20);
    list->horizontalScrollBar()->setValue(10);
    const auto vertical = list->verticalScrollBar()->value();
    const auto horizontal = list->horizontalScrollBar()->value();
    const auto current = list->currentItem()->text(0);
    require(vertical > 0, "Scroll fixture has nonzero scroll position");
    require(horizontal > 0, "Scroll fixture has nonzero horizontal scroll position");
    require(GrpFileManagerTest::save(window), "Save large archive");
    require(list->currentItem()->text(0) == current && list->selectedItems().size() == 2
            && list->verticalScrollBar()->value() == vertical
            && list->horizontalScrollBar()->value() == horizontal, "Save preserves multi-selection and both scroll positions");
    // Replacement uses a single selection, and retains viewport position on undo.
    list->clearSelection();
    list->currentItem()->setSelected(true);
    require(GrpFileManagerTest::replace(window, path("two/A.TXT")), "Replace scrolled entry");
    require(list->currentItem()->text(0) == current && list->verticalScrollBar()->value() == vertical,
            "Replacement preserves scrolled view");
    history->undo();
    require(list->currentItem()->text(0) == current && list->verticalScrollBar()->value() == vertical
            && !GrpFileManagerTest::dirty(window), "Undo restores scrolled view and saved contents");
    const auto index = history->index();
    require(choices({{"Skip", true}}, [&] { return GrpFileManagerTest::append(window, {path("one/A.TXT")}); }), "Skip conflict");
    require(history->index() == index && history->canRedo(), "No-op append preserves redo history");
    require(GrpFileManagerTest::load(window, archive) && !history->canUndo() && !history->canRedo(),
            "Opening an archive clears edit history");
    list->setCurrentItem(list->topLevelItem(0));
    list->topLevelItem(1)->setSelected(true);
    bool contextChecked = false;
    QTimer contextTimer;
    QObject::connect(&contextTimer, &QTimer::timeout, [&] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) return;
        require(menu->actions().contains(extractAction) && menu->actions().contains(replaceAction),
                "Context menu shares the main actions");
        require(list->selectedItems().size() == 2 && extractAction->isEnabled() && !replaceAction->isEnabled(),
                "Right click on selected entry preserves multi-selection and correct actions");
        contextChecked = true;
        menu->close();
    });
    contextTimer.start(1);
    QMetaObject::invokeMethod(list, "customContextMenuRequested", Qt::DirectConnection,
                             Q_ARG(QPoint, list->visualItemRect(list->topLevelItem(0)).center()));
    contextTimer.stop();
    require(contextChecked, "Context menu opened");
    // Current row can differ from the sole selected row after Ctrl-selection.
    list->clearSelection();
    list->topLevelItem(1)->setSelected(true);
    require(replaceAction->isEnabled(), "Replace enabled for one selected entry");
    require(GrpFileManagerTest::replace(window, path("one/B.TXT")), "Replace selected rather than current entry");
    require(list->topLevelItem(0)->text(3).isEmpty() && list->topLevelItem(1)->text(3) == "Replaced",
            "Only the selected entry is replaced");
    require(saveAction->isEnabled(), "Editing enables shared Save action");
    saveAction->trigger();
    require(!saveAction->isEnabled() && members(archive)[1].second == "original", "Save action commits archive and updates state");
    GrpFileManagerWindow previews;
    require(GrpFileManagerTest::create(previews, path("preview.grp")), "Create preview archive");
    QByteArray art;
    QDataStream artStream(&art, QIODevice::WriteOnly);
    artStream.setByteOrder(QDataStream::LittleEndian);
    artStream << qint32(1) << qint32(129) << qint32(0) << qint32(128);
    for (int i = 0; i < 129; ++i) { artStream << qint16(1); }
    for (int i = 0; i < 129; ++i) { artStream << qint16(1); }
    for (int i = 0; i < 129; ++i) { artStream << quint32(0); }
    art.append(QByteArray(129, char(1)));
    write(path("TILES.ART"), art);
    write(path("BAD.ART"), "invalid");
    write(path("GAME.CON"), "// <b>plain text</b>\n");
    write(path("BIG.TXT"), QByteArray(300 * 1024, 'a'));
    QByteArray map;
    QDataStream mapStream(&map, QIODevice::WriteOnly);
    mapStream.setByteOrder(QDataStream::LittleEndian);
    mapStream << qint32(7) << qint32(100) << qint32(200) << qint32(300)
              << qint16(0) << qint16(-1) << qint16(0) << quint16(0) << quint16(0);
    write(path("TEST.MAP"), map);
    require(GrpFileManagerTest::append(previews, {path("GAME.CON"), path("TILES.ART"),
        path("BAD.ART"), path("BIG.TXT"), path("TEST.MAP")}), "Append preview fixtures");
    auto *previewList = GrpFileManagerTest::list(previews);
    auto *textPreview = previews.findChild<QPlainTextEdit *>("grpTextPreview");
    auto *info = previews.findChild<QLabel *>("grpPreviewInfo");
    auto *tiles = previews.findChild<QListWidget *>("grpArtPreview");
    auto *page = previews.findChild<QSpinBox *>();
    previewList->setCurrentItem(previewList->topLevelItem(0));
    require(textPreview->toPlainText() == "// <b>plain text</b>\n" && textPreview->isReadOnly(), "Plain read-only CON preview");
    previewList->setCurrentItem(previewList->topLevelItem(1));
    require(tiles->count() == 128 && page->maximum() == 2 && info->text().contains("Grayscale"), "Paged grayscale ART preview");
    require(tiles->item(0)->icon().pixmap(64, 64).toImage().pixelColor(0, 0) == QColor(1, 1, 1), "ART pixel offset and grayscale");
    page->setValue(2);
    require(tiles->count() == 1 && tiles->item(0)->text().contains("128"), "Final ART page");
    QByteArray palette(768, char(0));
    palette[3] = 63;
    write(path("PALETTE.DAT"), palette);
    require(GrpFileManagerTest::append(previews, {path("PALETTE.DAT")}), "Append palette");
    previewList->setCurrentItem(previewList->topLevelItem(1));
    require(info->text().contains("Archive palette")
        && tiles->item(0)->icon().pixmap(64, 64).toImage().pixelColor(0, 0) == QColor(255, 0, 0), "Archive palette colors");
    previewList->setCurrentItem(previewList->topLevelItem(2));
    require(info->text().contains("Unable to preview"), "Malformed ART preview is non-modal");
    previewList->setCurrentItem(previewList->topLevelItem(3));
    require(textPreview->toPlainText().size() == 256 * 1024 && info->text().contains("limited"), "Bounded text preview");
    previewList->setCurrentItem(previewList->topLevelItem(4));
    require(info->text().contains("MAP version 7") && info->text().contains("100, 200, 300"), "MAP information preview");
    bool opened = false;
    previews.openMapRequested = [&](const QString &name, const QByteArray &data) {
        opened = name == "TEST.MAP" && data == map;
    };
    previewList->itemDoubleClicked(previewList->topLevelItem(4), 0);
    require(opened, "Double-click opens current unsaved MAP contents");
    previewList->topLevelItem(0)->setSelected(true);
    require(info->text().contains("Select one") && textPreview->isHidden() && tiles->isHidden(), "Multi-selection clears preview");
    std::cout << "GRP manager tests passed\n";
}
