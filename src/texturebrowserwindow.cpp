#include "texturebrowserwindow.h"

#include "texturebrowserwidget.h"

TextureBrowserWindow::TextureBrowserWindow(QWidget *parent)
    : QMainWindow(parent, Qt::Window)
    , m_browser(new TextureBrowserWidget(this))
{
    setWindowTitle("Texture Browser");
    setWindowModality(Qt::ApplicationModal);
    resize(800, 600);
    setCentralWidget(m_browser);
}

void TextureBrowserWindow::reload()
{
    m_browser->reload();
}
