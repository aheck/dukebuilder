#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;

class TexturePropertyEditor final : public QWidget
{
public:
    explicit TexturePropertyEditor(QWidget *parent);

    QLineEdit *value;
    QPushButton *browse;
};
