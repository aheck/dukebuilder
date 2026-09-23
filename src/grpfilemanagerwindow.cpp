#include "grpfilemanagerwindow.h"

#include <libduke/grp.h>

#include <QCloseEvent>
#include <QCheckBox>
#include <QAction>
#include <QMenuBar>
#include <QMenu>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QUndoStack>
#include <QDataStream>
#include <QDir>
#include <QDragEnterEvent>
#include <QDrag>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QToolBar>
#include <QStyle>
#include <QLocale>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace {
class GrpFileList final : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;
    std::function<void()> dragRequested;

protected:
    void startDrag(Qt::DropActions) override
    {
        if (dragRequested) dragRequested();
    }
};

struct GrpDeleter {
    void operator()(DukeGrpFile *file) const { duke_grp_free(file); }
};

using GrpPointer = std::unique_ptr<DukeGrpFile, GrpDeleter>;

enum class ConflictChoice { Replace, Skip, Cancel };

ConflictChoice resolveConflict(QWidget *parent, const QString &message,
                               std::optional<ConflictChoice> &remaining)
{
    if (remaining) return *remaining;
    QMessageBox dialog(QMessageBox::Question, "File already exists", message,
                       QMessageBox::NoButton, parent);
    dialog.setTextFormat(Qt::PlainText);
    auto *replace = dialog.addButton("Replace", QMessageBox::AcceptRole);
    auto *skip = dialog.addButton("Skip", QMessageBox::RejectRole);
    auto *cancel = dialog.addButton(QMessageBox::Cancel);
    auto *all = new QCheckBox("Apply to all remaining conflicts", &dialog);
    dialog.setCheckBox(all);
    dialog.setDefaultButton(skip);
    dialog.setEscapeButton(cancel);
    dialog.exec();
    const auto choice = dialog.clickedButton() == replace ? ConflictChoice::Replace
        : dialog.clickedButton() == skip ? ConflictChoice::Skip : ConflictChoice::Cancel;
    if (all->isChecked() && choice != ConflictChoice::Cancel) remaining = choice;
    return choice;
}

bool validMemberName(const QString &name)
{
    const QByteArray bytes = name.toLocal8Bit();
    if (bytes.isEmpty() || bytes.size() > 12) {
        return false;
    }
    for (const char character : bytes) {
        if (character <= 0x20 || character == '/' || character == '\\'
            || character == ':') {
            return false;
        }
    }
    return true;
}

QString errorText(const QString &operation, const QString &path)
{
    return QString("%1 '%2'.").arg(operation, path);
}
}

struct GrpFileManagerWindow::Member {
    QString name;
    QByteArray data;
    quint64 id = 0;
    bool operator==(const Member &other) const { return name == other.name && data == other.data && id == other.id; }
};

struct GrpFileManagerWindow::ViewState {
    std::vector<quint64> selected;
    quint64 current = 0;
    int vertical = 0;
    int horizontal = 0;
};

class GrpFileManagerWindow::EditCommand final : public QUndoCommand {
public:
    EditCommand(GrpFileManagerWindow *window, std::vector<Member> after, const QString &label)
        : QUndoCommand(label), m_window(window), m_before(*window->m_members),
          m_after(std::move(after)), m_beforeView(window->captureView()), m_afterView(m_beforeView) {}
    void undo() override { apply(m_before, m_beforeView); }
    void redo() override {
        apply(m_after, m_afterView);
        m_afterView = m_window->captureView();
    }
private:
    void apply(const std::vector<Member> &members, const ViewState &view) {
        *m_window->m_members = members;
        m_window->refreshList();
        m_window->restoreView(view);
        m_window->updateActions();
    }
    GrpFileManagerWindow *m_window;
    // QByteArray shares unchanged file contents between snapshots.
    std::vector<Member> m_before, m_after;
    ViewState m_beforeView, m_afterView;
};

GrpFileManagerWindow::ViewState GrpFileManagerWindow::captureView() const
{
    ViewState state;
    for (auto *item : m_files->selectedItems()) state.selected.push_back(item->data(0, Qt::UserRole + 1).toULongLong());
    if (auto *item = m_files->currentItem()) state.current = item->data(0, Qt::UserRole + 1).toULongLong();
    state.vertical = m_files->verticalScrollBar()->value();
    state.horizontal = m_files->horizontalScrollBar()->value();
    return state;
}

void GrpFileManagerWindow::restoreView(const ViewState &state)
{
    const QSignalBlocker blocker(m_files);
    m_files->clearSelection();
    for (int row = 0; row < m_files->topLevelItemCount(); ++row) {
        auto *item = m_files->topLevelItem(row);
        const auto id = item->data(0, Qt::UserRole + 1).toULongLong();
        if (id == state.current) m_files->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
        item->setSelected(std::find(state.selected.begin(), state.selected.end(), id) != state.selected.end());
    }
    m_files->doItemsLayout();
    m_files->verticalScrollBar()->setValue(state.vertical);
    m_files->horizontalScrollBar()->setValue(state.horizontal);
}

void GrpFileManagerWindow::commitEdit(std::vector<Member> members, const QString &label)
{
    if (members == *m_members) return;
    m_history->push(new EditCommand(this, std::move(members), label));
}

GrpFileManagerWindow::GrpFileManagerWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_members(new std::vector<Member>)
    , m_savedMembers(new std::vector<Member>)
{
    m_history = new QUndoStack(this);
    auto *fileMenu = menuBar()->addMenu("File");
    auto *editMenu = menuBar()->addMenu("Edit");
    auto *undoAction = m_history->createUndoAction(this, "Undo");
    undoAction->setShortcuts(QKeySequence::Undo);
    editMenu->addAction(undoAction);
    auto *redoAction = m_history->createRedoAction(this, "Redo");
    redoAction->setShortcuts(QKeySequence::Redo);
    editMenu->addAction(redoAction);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAcceptDrops(true);
    setWindowTitle("GRP File Manager");
    resize(720, 520);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    auto *description = new QLabel(
        "Drop a GRP file here to open it, or drop files to append them.", central);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *fileList = new GrpFileList(central);
    m_files = fileList;
    m_files->setColumnCount(4);
    m_files->setHeaderLabels({"Filename", "Type", "Size", "Change"});
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setAlternatingRowColors(true);
    m_files->setSortingEnabled(false);
    m_files->setRootIsDecorated(false);
    m_files->setDragEnabled(true);
    m_files->header()->setStretchLastSection(false);
    m_files->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_files->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_files->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_files->header()->resizeSection(0, 380);
    m_files->header()->resizeSection(1, 100);
    m_files->header()->resizeSection(2, 100);
    fileList->dragRequested = [this] { dragSelected(); };
    layout->addWidget(m_files, 1);

    setCentralWidget(central);
    const auto action = [this](const QString &name, const QString &id, QStyle::StandardPixmap icon) {
        auto *result = new QAction(style()->standardIcon(icon), name, this);
        result->setObjectName(id);
        return result;
    };
    auto *newAction = action("New archive…", "grpNewAction", QStyle::SP_FileIcon);
    auto *openAction = action("Open archive…", "grpOpenAction", QStyle::SP_DialogOpenButton);
    m_saveAction = action("Save", "grpSaveAction", QStyle::SP_DialogSaveButton);
    m_saveAsAction = action("Save As…", "grpSaveAsAction", QStyle::SP_DialogSaveButton);
    m_appendAction = action("Append files…", "grpAppendAction", QStyle::SP_ArrowDown);
    m_replaceAction = action("Replace selected file…", "grpReplaceAction", QStyle::SP_BrowserReload);
    m_deleteAction = action("Delete selected files", "grpDeleteAction", QStyle::SP_TrashIcon);
    m_extractAction = action("Extract selected files…", "grpExtractAction", QStyle::SP_ArrowUp);
    m_extractAllAction = action("Extract all files…", "grpExtractAllAction", QStyle::SP_ArrowUp);
    m_selectAllAction = new QAction("Select All", this);
    m_selectAllAction->setObjectName("grpSelectAllAction");
    newAction->setShortcuts(QKeySequence::New);
    openAction->setShortcuts(QKeySequence::Open);
    m_saveAction->setShortcuts(QKeySequence::Save);
    m_saveAsAction->setShortcuts(QKeySequence::SaveAs);
    m_deleteAction->setShortcut(QKeySequence(Qt::Key_Delete));
    m_selectAllAction->setShortcuts(QKeySequence::SelectAll);
    m_appendAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    m_extractAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_E));
    m_extractAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    fileMenu->addActions({newAction, openAction, m_saveAction, m_saveAsAction});
    fileMenu->addSeparator();
    fileMenu->addActions({m_extractAction, m_extractAllAction});
    editMenu->addSeparator();
    editMenu->addActions({m_appendAction, m_replaceAction, m_deleteAction});
    editMenu->addSeparator();
    editMenu->addAction(m_selectAllAction);
    auto *toolbar = addToolBar("Archive");
    toolbar->setObjectName("grpToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(20, 20));
    toolbar->addActions({newAction, openAction, m_saveAction});
    toolbar->addSeparator();
    toolbar->addActions({m_appendAction, m_extractAction, m_replaceAction, m_deleteAction});
    toolbar->addSeparator();
    undoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    redoAction->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    toolbar->addActions({undoAction, redoAction});
    m_files->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_files, &QWidget::customContextMenuRequested, this, [this, undoAction, redoAction](const QPoint &point) {
        if (auto *item = m_files->itemAt(point)) {
            m_files->setCurrentItem(item, 0, item->isSelected()
                ? QItemSelectionModel::NoUpdate : QItemSelectionModel::ClearAndSelect);
        }
        updateActions();
        QMenu menu(this);
        menu.addActions({m_extractAction, m_replaceAction, m_deleteAction});
        menu.addSeparator();
        menu.addActions({m_appendAction, m_extractAllAction, m_selectAllAction});
        menu.addSeparator();
        menu.addActions({undoAction, redoAction});
        menu.exec(m_files->viewport()->mapToGlobal(point));
    });

    connect(newAction, &QAction::triggered, this, [this] {
        if (!maybeDiscardChanges()) return;
        const QString path = QFileDialog::getSaveFileName(
            this, "Create GRP file", initialDirectory(), "GRP files (*.grp);;All files (*)");
        if (!path.isEmpty()) createArchive(path);
    });
    connect(openAction, &QAction::triggered, this, [this] {
        if (!maybeDiscardChanges()) return;
        const QString path = QFileDialog::getOpenFileName(
            this, "Open GRP file", initialDirectory(), "GRP files (*.grp *.GRP);;All files (*)");
        if (!path.isEmpty()) loadArchive(path);
    });
    connect(m_saveAction, &QAction::triggered, this, &GrpFileManagerWindow::saveArchive);
    connect(m_saveAsAction, &QAction::triggered, this, &GrpFileManagerWindow::saveArchiveAs);
    connect(m_extractAction, &QAction::triggered, this, [this] { extractSelected(false); });
    connect(m_extractAllAction, &QAction::triggered, this, [this] { extractSelected(true); });
    connect(m_deleteAction, &QAction::triggered, this, &GrpFileManagerWindow::deleteSelected);
    connect(m_selectAllAction, &QAction::triggered, m_files, &QTreeWidget::selectAll);
    connect(m_replaceAction, &QAction::triggered, this, [this] {
        const QString name = selectedMemberName();
        if (name.isEmpty()) return;
        const QString path = QFileDialog::getOpenFileName(this, "Replace " + name,
                                                           initialDirectory());
        if (!path.isEmpty()) replaceSelected(path);
    });
    connect(m_appendAction, &QAction::triggered, this, [this] {
        const QStringList paths = QFileDialog::getOpenFileNames(
            this, "Append files", initialDirectory());
        if (!paths.isEmpty()) appendFiles(paths);
    });
    connect(m_files, &QTreeWidget::itemSelectionChanged, this,
            &GrpFileManagerWindow::updateActions);
    updateActions();
}

GrpFileManagerWindow::~GrpFileManagerWindow()
{
    m_history->clear();
    delete m_savedMembers;
    delete m_members;
}

QString GrpFileManagerWindow::initialDirectory() const
{
    return m_archivePath.isEmpty() ? QStandardPaths::writableLocation(
        QStandardPaths::DocumentsLocation) : QFileInfo(m_archivePath).absolutePath();
}

void GrpFileManagerWindow::showError(const QString &message)
{
    QMessageBox::critical(this, "GRP File Manager", message);
}

bool GrpFileManagerWindow::maybeDiscardChanges()
{
    if (!m_dirty) return true;
    const auto answer = QMessageBox::question(this, "Unsaved changes",
        "Save changes to the GRP file before continuing?",
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
        QMessageBox::Save);
    if (answer == QMessageBox::Cancel) return false;
    return answer == QMessageBox::Discard || saveArchive();
}

bool GrpFileManagerWindow::loadArchive(const QString &path)
{
    GrpPointer archive(duke_grp_new());
    const QByteArray encoded = QFile::encodeName(path);
    if (!archive || !duke_grp_open_filename(archive.get(), encoded.constData())
        || std::memcmp(archive->header.magic, "KenSilverman", 12) != 0
        || !duke_grp_read_entries_full(archive.get())) {
        showError(errorText("Unable to read GRP file", path));
        return false;
    }

    std::vector<Member> members;
    members.reserve(archive->header.entry_count);
    for (uint32_t index = 0; index < archive->header.entry_count; ++index) {
        auto *entry = duke_grp_get_entry_by_index(archive.get(), index);
        void *data = nullptr;
        const size_t size = duke_grp_get_file_data_by_index(archive.get(), index, &data);
        if (!entry || size == static_cast<size_t>(-1)) {
            showError(errorText("Unable to read entries from GRP file", path));
            return false;
        }
        members.push_back({QString::fromLocal8Bit(entry->filename),
                           QByteArray(static_cast<const char *>(data),
                                      static_cast<qsizetype>(size)), m_nextMemberId++});
    }
    *m_members = std::move(members);
    m_archivePath = path;
    *m_savedMembers = *m_members;
    m_needsSave = false;
    m_history->clear();
    m_files->clear();
    refreshList();
    return true;
}

bool GrpFileManagerWindow::createArchive(const QString &path)
{
    m_archivePath = path;
    m_members->clear();
    m_savedMembers->clear();
    m_needsSave = true;
    m_history->clear();
    m_files->clear();
    refreshList();
    return saveArchive();
}

bool GrpFileManagerWindow::saveArchive()
{
    if (m_archivePath.isEmpty()) return saveArchiveAs();
    if (m_members->size() > static_cast<size_t>(UINT32_MAX)) {
        showError("The GRP file has too many entries.");
        return false;
    }
    QSaveFile output(m_archivePath);
    if (!output.open(QIODevice::WriteOnly)) {
        showError(errorText("Unable to write GRP file", m_archivePath));
        return false;
    }
    QDataStream stream(&output);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("KenSilverman", 12);
    stream << static_cast<quint32>(m_members->size());
    for (const Member &member : *m_members) {
        QByteArray name = member.name.toLocal8Bit();
        name.resize(12);
        stream.writeRawData(name.constData(), 12);
        stream << static_cast<quint32>(member.data.size());
    }
    for (const Member &member : *m_members) {
        if (stream.writeRawData(member.data.constData(), member.data.size()) != member.data.size()) {
            showError(errorText("Unable to write GRP file", m_archivePath));
            return false;
        }
    }
    if (stream.status() != QDataStream::Ok || !output.commit()) {
        showError(errorText("Unable to write GRP file", m_archivePath));
        return false;
    }
    *m_savedMembers = *m_members;
    m_needsSave = false;
    refreshList();
    return true;
}

bool GrpFileManagerWindow::saveArchiveAs()
{
    const QString path = QFileDialog::getSaveFileName(
        this, "Save GRP file", m_archivePath.isEmpty() ? initialDirectory() : m_archivePath,
        "GRP files (*.grp);;All files (*)");
    if (path.isEmpty()) return false;
    m_archivePath = path;
    return saveArchive();
}

bool GrpFileManagerWindow::appendFiles(const QStringList &paths)
{
    // Stage the entire batch so Cancel or an input error preserves the archive.
    auto updated = *m_members;
    std::optional<ConflictChoice> remaining;
    bool changed = false;
    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();
        if (!validMemberName(name)) {
            showError(QString("'%1' is not a valid GRP member name. Names must be 1–12 characters.").arg(name));
            return false;
        }
        const auto existing = std::find_if(updated.begin(), updated.end(), [&](const Member &member) {
            return member.name == name;
        });
        if (existing != updated.end()) {
            const auto choice = resolveConflict(this,
                QString("'%1' already exists in this archive. Replace it with '%2'?").arg(name, path), remaining);
            if (choice == ConflictChoice::Cancel) return false;
            if (choice == ConflictChoice::Skip) continue;
        }
        QFile input(path);
        if (!input.open(QIODevice::ReadOnly)) {
            showError(errorText("Unable to read file", path));
            return false;
        }
        const QByteArray data = input.readAll();
        if (input.error() != QFileDevice::NoError) {
            showError(errorText("Unable to read file", path));
            return false;
        }
        if (static_cast<quint64>(data.size()) > UINT32_MAX) {
            showError("GRP files cannot contain members larger than 4 GiB.");
            return false;
        }
        if (existing != updated.end()) {
            changed = changed || existing->data != data;
            existing->data = data;
        } else {
            updated.push_back({name, data, m_nextMemberId++});
            changed = true;
        }
    }
    if (!changed) return true;
    commitEdit(std::move(updated), "Append / replace files");
    return true;
}

bool GrpFileManagerWindow::replaceSelected(const QString &path)
{
    const QString name = selectedMemberName();
    if (name.isEmpty()) return false;
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        showError(errorText("Unable to read file", path));
        return false;
    }
    const QByteArray data = input.readAll();
    if (input.error() != QFileDevice::NoError) {
        showError(errorText("Unable to read file", path));
        return false;
    }
    if (static_cast<quint64>(data.size()) > UINT32_MAX) {
        showError("GRP files cannot contain members larger than 4 GiB.");
        return false;
    }
    const int row = m_files->indexOfTopLevelItem(m_files->selectedItems().front());
    auto updated = *m_members;
    updated[static_cast<size_t>(row)].data = data;
    commitEdit(std::move(updated), "Replace file");
    return true;
}

void GrpFileManagerWindow::dragSelected()
{
    const auto selected = m_files->selectedItems();
    if (selected.isEmpty()) return;

    // Keep these files alive after QDrag::exec() returns. Some file managers
    // consume URI-list sources asynchronously after accepting the drop.
    m_dragDirectory = std::make_unique<QTemporaryDir>(
        QDir::tempPath() + "/dukebuilder-grp-XXXXXX");
    if (!m_dragDirectory->isValid()) {
        m_dragDirectory.reset();
        showError("Unable to prepare files for dragging.");
        return;
    }

    QList<QUrl> urls;
    for (auto *item : selected) {
        const int row = m_files->indexOfTopLevelItem(item);
        if (row < 0) continue;
        const Member &member = (*m_members)[static_cast<size_t>(row)];
        const QString path = QDir(m_dragDirectory->path()).filePath(member.name);
        QFile output(path);
        if (!output.open(QIODevice::WriteOnly)
            || output.write(member.data) != member.data.size()) {
            showError(errorText("Unable to prepare file for dragging", member.name));
            return;
        }
        urls.append(QUrl::fromLocalFile(path));
    }

    if (urls.isEmpty()) return;
    QMimeData *mimeData = new QMimeData;
    mimeData->setUrls(urls);
    // Use the view as the drag source, as native item views do. This matters
    // to some desktop file managers when determining the source window.
    auto *drag = new QDrag(m_files);
    drag->setMimeData(mimeData);
    const QString dragLabel = selected.size() == 1
        ? selected.front()->text(0)
        : QString("%1 files").arg(selected.size());
    const QFontMetrics metrics(font());
    const int dragWidth = std::clamp(metrics.horizontalAdvance(dragLabel) + 28, 96, 260);
    QPixmap dragPixmap(dragWidth, 34);
    dragPixmap.fill(Qt::transparent);
    QPainter painter(&dragPixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(palette().color(QPalette::HighlightedText));
    painter.setBrush(palette().color(QPalette::Highlight));
    painter.drawRoundedRect(QRectF(1, 1, dragWidth - 2, 32), 6, 6);
    painter.drawText(QRect(14, 1, dragWidth - 22, 32), Qt::AlignVCenter,
                     metrics.elidedText(dragLabel, Qt::ElideRight, dragWidth - 22));
    painter.end();
    drag->setPixmap(dragPixmap);
    drag->setHotSpot(QPoint(14, 17));
    drag->exec(Qt::CopyAction | Qt::MoveAction);
}

void GrpFileManagerWindow::deleteSelected()
{
    const auto selected = m_files->selectedItems();
    if (selected.isEmpty()) return;
    if (QMessageBox::question(this, "Delete GRP files",
            QString("Delete %1 selected file(s) from the archive?").arg(selected.size()))
        != QMessageBox::Yes) return;
    std::vector<int> rows;
    rows.reserve(selected.size());
    for (auto *item : selected) rows.push_back(m_files->indexOfTopLevelItem(item));
    std::sort(rows.rbegin(), rows.rend());
    auto updated = *m_members;
    for (const int row : rows) updated.erase(updated.begin() + row);
    commitEdit(std::move(updated), "Delete files");
}

void GrpFileManagerWindow::extractSelected(bool all)
{
    std::vector<int> rows;
    if (all) {
        for (int row = 0; row < m_files->topLevelItemCount(); ++row) rows.push_back(row);
    } else {
        for (auto *item : m_files->selectedItems()) rows.push_back(m_files->indexOfTopLevelItem(item));
    }
    if (rows.empty()) return;
    const QString directory = QFileDialog::getExistingDirectory(this, "Extract GRP files",
                                                                 initialDirectory());
    if (directory.isEmpty()) return;
    extractFiles(rows, directory);
}

bool GrpFileManagerWindow::extractFiles(const std::vector<int> &rows, const QString &directory)
{
    std::optional<ConflictChoice> remaining;
    std::vector<std::pair<QString, int>> outputs;
    // Resolve all conflicts before writing anything, so cancelling this prompt
    // does not leave a partially extracted batch.
    for (const int row : rows) {
        const Member &member = (*m_members)[static_cast<size_t>(row)];
        if (!validMemberName(member.name) || member.name == "." || member.name == "..") {
            showError("Cannot extract an invalid member name: " + member.name);
            return false;
        }
        const QString path = QDir(directory).filePath(member.name);
        const auto planned = std::find_if(outputs.begin(), outputs.end(), [&](const auto &output) {
            return output.first == path;
        });
        if (QFileInfo::exists(path) || QFileInfo(path).isSymLink() || planned != outputs.end()) {
            const auto choice = resolveConflict(this,
                QString("'%1' already exists or is included earlier in this extraction. Replace it?").arg(path), remaining);
            if (choice == ConflictChoice::Cancel) return false;
            if (choice == ConflictChoice::Skip) continue;
        }
        if (planned != outputs.end()) planned->second = row;
        else outputs.emplace_back(path, row);
    }
    for (const auto &[path, row] : outputs) {
        const auto &member = (*m_members)[static_cast<size_t>(row)];
        QSaveFile output(path);
        if (!output.open(QIODevice::WriteOnly) || output.write(member.data) != member.data.size()
            || !output.commit()) {
            showError(errorText("Unable to extract file", member.name));
            return false;
        }
    }
    return true;
}

void GrpFileManagerWindow::refreshList()
{
    const auto view = captureView();
    const QSignalBlocker blocker(m_files);
    m_dirty = m_needsSave || *m_members != *m_savedMembers;
    int added = 0, replaced = 0, deleted = 0;
    m_files->clear();
    for (const Member &member : *m_members) {
        auto *item = new QTreeWidgetItem(m_files);
        const QString extension = QFileInfo(member.name).suffix();
        item->setText(0, member.name);
        item->setText(1, extension.isEmpty() ? "(none)" : extension);
        item->setText(2, QString::number(member.data.size()) + " bytes");
        item->setData(0, Qt::UserRole, member.name);
        item->setData(0, Qt::UserRole + 1, member.id);
        const auto saved = std::find_if(m_savedMembers->begin(), m_savedMembers->end(),
                                       [&](const Member &old) { return old.id == member.id; });
        if (saved == m_savedMembers->end()) { item->setText(3, "Added"); ++added; }
        else if (saved->data != member.data) { item->setText(3, "Replaced"); ++replaced; }
        if (!item->text(3).isEmpty()) {
            auto font = item->font(0);
            font.setBold(true);
            item->setFont(0, font);
        }
    }
    for (const auto &saved : *m_savedMembers)
        if (std::none_of(m_members->begin(), m_members->end(), [&](const Member &member) { return member.id == saved.id; })) ++deleted;
    restoreView(view);
    m_pendingChanges = added || replaced || deleted
        ? QString(" · %1 added · %2 replaced · %3 deleted").arg(added).arg(replaced).arg(deleted) : QString();
    setWindowTitle((m_dirty ? "* " : "") + QString("GRP File Manager")
                   + (m_archivePath.isEmpty() ? QString() : " — " + QFileInfo(m_archivePath).fileName()));
    updateActions();
}

QString GrpFileManagerWindow::selectedMemberName() const
{
    const auto selected = m_files->selectedItems();
    const auto *item = selected.size() == 1 ? selected.front() : nullptr;
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void GrpFileManagerWindow::updateActions()
{
    const bool hasArchive = !m_archivePath.isEmpty();
    const bool hasSelection = !m_files->selectedItems().isEmpty();
    m_saveAction->setEnabled(hasArchive && m_dirty);
    m_saveAsAction->setEnabled(hasArchive);
    m_appendAction->setEnabled(hasArchive);
    m_replaceAction->setEnabled(m_files->selectedItems().size() == 1);
    m_deleteAction->setEnabled(hasSelection);
    m_extractAction->setEnabled(hasSelection);
    m_extractAllAction->setEnabled(hasArchive && !m_members->empty());
    m_selectAllAction->setEnabled(!m_members->empty());
    qint64 bytes = 0;
    for (const auto &member : *m_members) bytes += member.data.size();
    statusBar()->showMessage(QString("%1 files · %2 · %3 selected%4")
        .arg(m_members->size()).arg(QLocale().formattedDataSize(bytes))
        .arg(m_files->selectedItems().size()).arg(m_pendingChanges));
}

void GrpFileManagerWindow::closeEvent(QCloseEvent *event)
{
    if (maybeDiscardChanges()) {
        hide();
        event->ignore();
    } else {
        event->ignore();
    }
}

void GrpFileManagerWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void GrpFileManagerWindow::dropEvent(QDropEvent *event)
{
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile()) paths.append(url.toLocalFile());
    }
    if (paths.isEmpty()) return;
    if (paths.size() == 1 && QFileInfo(paths.first()).suffix().compare("grp", Qt::CaseInsensitive) == 0) {
        if (!maybeDiscardChanges()) {
            event->ignore();
            return;
        }
        loadArchive(paths.first());
    } else if (!m_archivePath.isEmpty()) {
        appendFiles(paths);
    }
    event->acceptProposedAction();
}
