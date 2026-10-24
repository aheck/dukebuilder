#include "propertyvaluedelegate.h"
#include "mapeditor.h"
#include "spritelotags.h"
#include "tags.h"
#include "texturepropertyeditor.h"
#include "texturevalueedit.h"
#include <QComboBox>
#include <QPersistentModelIndex>
#include <QPushButton>
#include <QSignalBlocker>
#include <utility>

void PropertyValueDelegate::setTextureChooser(
    std::function<std::optional<int>(std::optional<int>)> chooser)
{
    m_textureChooser = std::move(chooser);
}

QWidget *PropertyValueDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                      const QModelIndex &index) const
{
    if (index.column() != 1) {
        return nullptr;
    }
    const auto property = static_cast<MapEditor::Property>(
        index.siblingAtColumn(0).data(Qt::UserRole).toInt());
    if (property == MapEditor::Property::Lotag
        || property == MapEditor::Property::SectorLotag
        || property == MapEditor::Property::WallLotag) {
        auto *editor = new QComboBox(parent);
        editor->setEditable(true);
        editor->setInsertPolicy(QComboBox::NoInsert);
        // Descriptive preset labels must not autocomplete over numeric input.
        editor->setCompleter(nullptr);
        editor->setMinimumContentsLength(12);
        editor->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        if (property == MapEditor::Property::WallLotag) {
            // Wall tags are texture-dependent channels or sound IDs, not sector effects.
            // https://github.com/jonof/jfduke3d/blob/master/src/sector.c
            editor->setToolTip("Wall lotags depend on the texture: switches, doors and "
                               "forcefields use matching channel numbers; mirrors use sound IDs. "
                               "Enter -32768 to 32767, or 65535 for an exit switch.");
            editor->addItem("0 - No tag", 0);
            editor->addItem("1 - Channel 1 (switch / door / forcefield example)", 1);
            editor->addItem("252 - Mirror: play sound 252 on use", 252);
            editor->addItem("-1 (65535) - Switch / door: end level", -1);
        } else if (property == MapEditor::Property::SectorLotag) {
            editor->setToolTip("Duke 3D sector lotags. Enter an integer from 0 to 65535. "
                               "For a one-time sound, enter 10000 + sound ID (10xxx).");
            for (const auto &entry : sectorLotags) {
                editor->addItem(QString::number(entry.tag) + " - " + entry.meaning,
                                entry.tag);
            }
        } else {
            const auto tile = index.siblingAtColumn(0).data(Qt::UserRole + 2);
            const auto tags = spriteLotags(tile.isValid() ? tile.toInt() : -1);
            editor->setToolTip(QString::fromUtf8(tags.description)
                + " Standard Duke 3D / Atomic meanings; mods may redefine them. "
                  "Enter -32768 to 32767, or 65535 as an alias for -1.");
            editor->lineEdit()->setPlaceholderText("Enter lotag");
            for (const auto &entry : tags.presets) {
                editor->addItem(QString::number(entry.tag) + " - " + entry.meaning, entry.tag);
            }
        }
        const auto commit = [this, editor, index = QPersistentModelIndex(index)] {
            if (index.isValid()) {
                emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
            }
        };
        connect(editor, &QComboBox::activated, editor, commit, Qt::QueuedConnection);
        connect(editor->lineEdit(), &QLineEdit::editingFinished, editor, commit);
        return editor;
    }
    if (property != MapEditor::Property::Texture
        && property != MapEditor::Property::FloorTexture
        && property != MapEditor::Property::CeilingTexture
        && property != MapEditor::Property::OverlayTexture) {
        return QStyledItemDelegate::createEditor(parent, option, index);
    }

    auto *editor = new TexturePropertyEditor(parent);
    static_cast<TextureValueEdit *>(editor->value)->tileDropped =
        [this, editor, index = QPersistentModelIndex(index)](int) {
            if (index.isValid()) {
                emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
            }
        };
    connect(editor->value, &QLineEdit::editingFinished, editor,
            [this, editor, index = QPersistentModelIndex(index)] {
                if (index.isValid()) {
                    emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                }
            });
    connect(editor->browse, &QPushButton::clicked, editor,
            [this, editor, index = QPersistentModelIndex(index)] {
                bool valid = false;
                const int currentTexture = editor->value->text().toInt(&valid);
                if (!m_textureChooser) {
                    return;
                }
                const std::optional<int> texture = m_textureChooser(
                    valid ? std::optional<int>(currentTexture) : std::nullopt);
                if (texture && index.isValid()) {
                    editor->value->setText(QString::number(*texture));
                    emit const_cast<PropertyValueDelegate *>(this)->commitData(editor);
                }
            });
    return editor;
}

void PropertyValueDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    if (auto *lotagEditor = dynamic_cast<QComboBox *>(editor)) {
        const QSignalBlocker blocker(lotagEditor);
        const int tag = index.data(Qt::EditRole).toInt();
        const int entry = lotagEditor->findData(tag);
        lotagEditor->setCurrentIndex(entry);
        if (entry < 0) {
            lotagEditor->setEditText(QString::number(tag));
        }
        return;
    }
    if (auto *textureEditor = dynamic_cast<TexturePropertyEditor *>(editor)) {
        textureEditor->value->setText(index.data(Qt::EditRole).toString());
        return;
    }
    QStyledItemDelegate::setEditorData(editor, index);
}

void PropertyValueDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                  const QModelIndex &index) const
{
    if (auto *lotagEditor = dynamic_cast<QComboBox *>(editor)) {
        const QString text = lotagEditor->currentText().trimmed();
        const int entry = lotagEditor->findText(text, Qt::MatchExactly);
        bool valid = false;
        int tag = entry >= 0 ? lotagEditor->itemData(entry).toInt(&valid)
                                   : text.toInt(&valid);
        const auto property = static_cast<MapEditor::Property>(
            index.siblingAtColumn(0).data(Qt::UserRole).toInt());
        if ((property == MapEditor::Property::WallLotag
             || property == MapEditor::Property::Lotag) && tag == 65535) {
            tag = -1;
        }
        const int maximum = property == MapEditor::Property::SectorLotag ? 65535 : 32767;
        if (valid && tag >= -32768 && tag <= maximum) {
            model->setData(index, QString::number(tag), Qt::EditRole);
        } else {
            setEditorData(editor, index);
        }
        return;
    }
    if (auto *textureEditor = dynamic_cast<TexturePropertyEditor *>(editor)) {
        model->setData(index, textureEditor->value->text(), Qt::EditRole);
        return;
    }
    QStyledItemDelegate::setModelData(editor, model, index);
}
