#include "grpfilemanagerwindow.h"

#include <libduke/grp.h>

#include <QCloseEvent>
#include <QDataStream>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

namespace {
struct GrpDeleter {
    void operator()(DukeGrpFile *file) const { duke_grp_free(file); }
};

using GrpPointer = std::unique_ptr<DukeGrpFile, GrpDeleter>;

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
};

GrpFileManagerWindow::GrpFileManagerWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_members(new std::vector<Member>)
{
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

    m_files = new QTreeWidget(central);
    m_files->setColumnCount(3);
    m_files->setHeaderLabels({"Filename", "Type", "Size"});
    m_files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_files->setAlternatingRowColors(true);
    m_files->setSortingEnabled(false);
    m_files->setRootIsDecorated(false);
    m_files->header()->setStretchLastSection(false);
    m_files->header()->setSectionResizeMode(0, QHeaderView::Interactive);
    m_files->header()->setSectionResizeMode(1, QHeaderView::Interactive);
    m_files->header()->setSectionResizeMode(2, QHeaderView::Interactive);
    m_files->header()->resizeSection(0, 380);
    m_files->header()->resizeSection(1, 100);
    m_files->header()->resizeSection(2, 100);
    layout->addWidget(m_files, 1);

    auto *buttons = new QHBoxLayout;
    auto *newButton = new QPushButton("New", central);
    auto *openButton = new QPushButton("Open", central);
    auto *saveButton = new QPushButton("Save", central);
    auto *saveAsButton = new QPushButton("Save As", central);
    auto *extractButton = new QPushButton("Extract Selected", central);
    auto *extractAllButton = new QPushButton("Extract All", central);
    auto *deleteButton = new QPushButton("Delete", central);
    auto *replaceButton = new QPushButton("Replace", central);
    auto *appendButton = new QPushButton("Append", central);
    for (auto *button : {newButton, openButton, saveButton, saveAsButton,
                         extractButton, extractAllButton, deleteButton,
                         replaceButton, appendButton}) {
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    setCentralWidget(central);

    connect(newButton, &QPushButton::clicked, this, [this] {
        if (!maybeDiscardChanges()) return;
        const QString path = QFileDialog::getSaveFileName(
            this, "Create GRP file", initialDirectory(), "GRP files (*.grp);;All files (*)");
        if (!path.isEmpty()) createArchive(path);
    });
    connect(openButton, &QPushButton::clicked, this, [this] {
        if (!maybeDiscardChanges()) return;
        const QString path = QFileDialog::getOpenFileName(
            this, "Open GRP file", initialDirectory(), "GRP files (*.grp *.GRP);;All files (*)");
        if (!path.isEmpty()) loadArchive(path);
    });
    connect(saveButton, &QPushButton::clicked, this, &GrpFileManagerWindow::saveArchive);
    connect(saveAsButton, &QPushButton::clicked, this, &GrpFileManagerWindow::saveArchiveAs);
    connect(extractButton, &QPushButton::clicked, this, [this] { extractSelected(false); });
    connect(extractAllButton, &QPushButton::clicked, this, [this] { extractSelected(true); });
    connect(deleteButton, &QPushButton::clicked, this, &GrpFileManagerWindow::deleteSelected);
    connect(replaceButton, &QPushButton::clicked, this, [this] {
        const QString name = selectedMemberName();
        if (name.isEmpty()) return;
        const QString path = QFileDialog::getOpenFileName(this, "Replace " + name,
                                                           initialDirectory());
        if (!path.isEmpty()) replaceSelected(path);
    });
    connect(appendButton, &QPushButton::clicked, this, [this] {
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
                                      static_cast<qsizetype>(size))});
    }
    *m_members = std::move(members);
    m_archivePath = path;
    m_dirty = false;
    refreshList();
    return true;
}

bool GrpFileManagerWindow::createArchive(const QString &path)
{
    m_archivePath = path;
    m_members->clear();
    m_dirty = true;
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
    m_dirty = false;
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
    std::vector<Member> additions;
    for (const QString &path : paths) {
        const QString name = QFileInfo(path).fileName();
        if (!validMemberName(name)) {
            showError(QString("'%1' is not a valid GRP member name. Names must be 1–12 characters.").arg(name));
            return false;
        }
        if (std::any_of(m_members->begin(), m_members->end(), [&](const Member &member) {
                return member.name == name;
            }) || std::any_of(additions.begin(), additions.end(), [&](const Member &member) {
                return member.name == name;
            })) {
            showError("A file with the name '" + name + "' is already in the archive.");
            return false;
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
        additions.push_back({name, data});
    }
    m_members->insert(m_members->end(), additions.begin(), additions.end());
    m_dirty = true;
    refreshList();
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
    const int row = m_files->indexOfTopLevelItem(m_files->currentItem());
    (*m_members)[static_cast<size_t>(row)].data = data;
    m_dirty = true;
    refreshList();
    m_files->setCurrentItem(m_files->topLevelItem(row));
    return true;
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
    for (const int row : rows) m_members->erase(m_members->begin() + row);
    m_dirty = true;
    refreshList();
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
    for (const int row : rows) {
        const Member &member = (*m_members)[static_cast<size_t>(row)];
        QFile output(QDir(directory).filePath(member.name));
        if (!output.open(QIODevice::WriteOnly) || output.write(member.data) != member.data.size()) {
            showError(errorText("Unable to extract file", member.name));
            return;
        }
    }
}

void GrpFileManagerWindow::refreshList()
{
    m_files->clear();
    for (const Member &member : *m_members) {
        auto *item = new QTreeWidgetItem(m_files);
        const QString extension = QFileInfo(member.name).suffix();
        item->setText(0, member.name);
        item->setText(1, extension.isEmpty() ? "(none)" : extension);
        item->setText(2, QString::number(member.data.size()) + " bytes");
        item->setData(0, Qt::UserRole, member.name);
    }
    setWindowTitle((m_dirty ? "* " : "") + QString("GRP File Manager")
                   + (m_archivePath.isEmpty() ? QString() : " — " + QFileInfo(m_archivePath).fileName()));
    updateActions();
}

QString GrpFileManagerWindow::selectedMemberName() const
{
    const auto *item = m_files->currentItem();
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

void GrpFileManagerWindow::updateActions()
{
    const bool hasArchive = !m_archivePath.isEmpty();
    const bool hasSelection = !m_files->selectedItems().isEmpty();
    for (auto *button : findChildren<QPushButton *>()) {
        if (button->text() == "Save") button->setEnabled(hasArchive && m_dirty);
        else if (button->text() == "Save As") button->setEnabled(hasArchive);
        else if (button->text() == "Extract Selected" || button->text() == "Delete") button->setEnabled(hasSelection);
        else if (button->text() == "Extract All") button->setEnabled(hasArchive && !m_members->empty());
        else if (button->text() == "Replace") button->setEnabled(hasSelection && m_files->selectedItems().size() == 1);
        else if (button->text() == "Append") button->setEnabled(hasArchive);
    }
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
    if (paths.size() == 1 && QFileInfo(paths.first()).suffix().compare("grp", Qt::CaseInsensitive) == 0
        && maybeDiscardChanges()) {
        loadArchive(paths.first());
    } else if (!m_archivePath.isEmpty()) {
        appendFiles(paths);
    }
    event->acceptProposedAction();
}
