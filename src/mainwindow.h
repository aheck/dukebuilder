#pragma once

#include <QMainWindow>

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);

private:
    QString m_mapFilename;
};
