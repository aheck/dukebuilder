#pragma once

#include <QMainWindow>

#include <memory>
#include <vector>

class QCloseEvent;
class QDragEnterEvent;
class QDropEvent;
class QTreeWidget;
class QTreeWidgetItem;
class QTemporaryDir;

class GrpFileManagerWindow final : public QMainWindow
{
public:
    explicit GrpFileManagerWindow(QWidget *parent = nullptr);
    ~GrpFileManagerWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    friend struct GrpFileManagerTest;
    struct Member;

    bool maybeDiscardChanges();
    bool saveArchive();
    bool saveArchiveAs();
    bool loadArchive(const QString &path);
    bool createArchive(const QString &path);
    bool appendFiles(const QStringList &paths);
    bool replaceSelected(const QString &path);
    void dragSelected();
    void deleteSelected();
    void extractSelected(bool all);
    bool extractFiles(const std::vector<int> &rows, const QString &directory);
    void refreshList();
    void updateActions();
    void showError(const QString &message);
    QString selectedMemberName() const;
    QString initialDirectory() const;

    QTreeWidget *m_files = nullptr;
    QString m_archivePath;
    std::vector<Member> *m_members = nullptr;
    std::unique_ptr<QTemporaryDir> m_dragDirectory;
    bool m_dirty = false;
};
