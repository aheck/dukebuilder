#pragma once

#include <QImage>
#include <QMap>
#include <QWidget>

#include <optional>

class QLabel;
class QLineEdit;
class QListWidget;

class TextureBrowserWidget final : public QWidget
{
public:
    explicit TextureBrowserWidget(QWidget *parent = nullptr);

    void reload();
    [[nodiscard]] std::optional<int> selectedTile() const;
    [[nodiscard]] QImage textureImage(int tile) const;
    void selectTile(int tile);

private:
    void updateFilter(const QString &text);

    QLineEdit *m_filter = nullptr;
    QListWidget *m_textureList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QMap<int, QImage> m_images;
};
