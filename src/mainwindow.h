#pragma once

#include <QMainWindow>
#include <functional>
#include <memory>
class RecoveryFile;
class GrpFileManagerWindow;
class QTimer;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QTimer *m_autosaveTimer = nullptr;
    std::unique_ptr<RecoveryFile> m_recovery;
    std::unique_ptr<GrpFileManagerWindow> m_grpFileManager;
    QString m_recoveryOrigin;
    QString m_mapFilename;
    std::function<bool()> m_confirmUnsavedChanges;
};
