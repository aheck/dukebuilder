#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QListWidget;

class TextureBrowserWidget final : public QWidget
{
public:
    explicit TextureBrowserWidget(QWidget *parent = nullptr);

    void reload();

private:
    void updateFilter(const QString &text);

    QLineEdit *m_filter = nullptr;
    QListWidget *m_textureList = nullptr;
    QLabel *m_statusLabel = nullptr;
};
