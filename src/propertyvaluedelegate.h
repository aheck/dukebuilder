#pragma once

#include <QStyledItemDelegate>
#include <functional>
#include <optional>

class PropertyValueDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void setTextureChooser(std::function<std::optional<int>(std::optional<int>)> chooser);
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

private:
    std::function<std::optional<int>(std::optional<int>)> m_textureChooser;
};
