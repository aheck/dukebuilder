#include "grpfilemanagerwindow.h"
#include <libduke/grp.h>
#include <QApplication>
#include <QAbstractButton>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
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
    std::cout << "GRP conflict handling passed\n";
}
