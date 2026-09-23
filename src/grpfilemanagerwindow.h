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
class QUndoStack;
class QAction;

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
    struct ViewState;
    class EditCommand;
    ViewState captureView() const;
    void restoreView(const ViewState &state);
    void commitEdit(std::vector<Member> members, const QString &label);

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
    std::vector<Member> *m_savedMembers = nullptr;
    QUndoStack *m_history = nullptr;
    QAction *m_saveAction = nullptr;
    QAction *m_saveAsAction = nullptr;
    QAction *m_appendAction = nullptr;
    QAction *m_replaceAction = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_extractAction = nullptr;
    QAction *m_extractAllAction = nullptr;
    QAction *m_selectAllAction = nullptr;
    QString m_pendingChanges;
    quint64 m_nextMemberId = 1;
    bool m_needsSave = false;
    std::unique_ptr<QTemporaryDir> m_dragDirectory;
    bool m_dirty = false;
};
