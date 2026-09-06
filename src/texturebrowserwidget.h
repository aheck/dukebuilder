#pragma once

#include <QImage>
#include <QMap>
#include <QWidget>

#include <optional>
#include <functional>
#include <utility>

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
    void setTextureActivationCallback(std::function<void()> callback)
    {
        m_textureActivationCallback = std::move(callback);
    }

private:
    void updateFilter(const QString &text);

    QLineEdit *m_filter = nullptr;
    QListWidget *m_textureList = nullptr;
    QLabel *m_statusLabel = nullptr;
    QMap<int, QImage> m_images;
    std::function<void()> m_textureActivationCallback;
};
