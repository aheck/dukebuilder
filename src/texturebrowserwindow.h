#pragma once

#include <QMainWindow>

class TextureBrowserWidget;

class TextureBrowserWindow final : public QMainWindow
{
public:
    explicit TextureBrowserWindow(QWidget *parent = nullptr);

    void reload();

private:
    TextureBrowserWidget *m_browser = nullptr;
};
