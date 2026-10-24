#pragma once

#include <QLineEdit>
#include <functional>

class TextureValueEdit final : public QLineEdit
{
public:
    using QLineEdit::QLineEdit;
    std::function<void(int)> tileDropped;

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
};
