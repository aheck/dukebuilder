#include "texturepropertyeditor.h"
#include "texturevalueedit.h"
#include <QHBoxLayout>
#include <QPushButton>

TexturePropertyEditor::TexturePropertyEditor(QWidget *parent)
    : QWidget(parent)
    , value(new TextureValueEdit(this))
    , browse(new QPushButton("...", this))
{
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    browse->setFixedWidth(28);
    browse->setToolTip("Choose texture");
    layout->addWidget(value, 1);
    layout->addWidget(browse);
}
