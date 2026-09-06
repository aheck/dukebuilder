#pragma once

#include <QMainWindow>
#include <functional>

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QString m_mapFilename;
    std::function<bool()> m_confirmUnsavedChanges;
};
