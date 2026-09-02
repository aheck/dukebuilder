#include "settingsdialog.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace {
constexpr auto grpFilesSettingsKey = "gameData/grpFiles";
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Settings");
    resize(720, 420);

    auto *categoryList = new QListWidget(this);
    categoryList->setFixedWidth(160);
    categoryList->addItem("Game Data");

    auto *pages = new QStackedWidget(this);
    auto *gameDataPage = new QWidget(pages);
    auto *gameDataLayout = new QVBoxLayout(gameDataPage);
    auto *heading = new QLabel("GRP files", gameDataPage);
    auto *grpFiles = new QListWidget(gameDataPage);
    grpFiles->setSelectionMode(QAbstractItemView::ExtendedSelection);

    QSettings settings;
    grpFiles->addItems(settings.value(grpFilesSettingsKey).toStringList());

    auto *addButton = new QPushButton("Add...", gameDataPage);
    auto *removeButton = new QPushButton("Remove", gameDataPage);
    removeButton->setEnabled(false);

    auto *buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(addButton);
    buttonLayout->addWidget(removeButton);
    buttonLayout->addStretch();

    gameDataLayout->addWidget(heading);
    gameDataLayout->addWidget(grpFiles, 1);
    gameDataLayout->addLayout(buttonLayout);
    pages->addWidget(gameDataPage);

    auto *contentLayout = new QHBoxLayout();
    contentLayout->addWidget(categoryList);
    contentLayout->addWidget(pages, 1);

    auto *dialogButtons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(dialogButtons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(contentLayout, 1);
    layout->addWidget(dialogButtons);

    connect(categoryList, &QListWidget::currentRowChanged,
            pages, &QStackedWidget::setCurrentIndex);
    connect(grpFiles, &QListWidget::itemSelectionChanged, this,
            [grpFiles, removeButton] {
                removeButton->setEnabled(!grpFiles->selectedItems().isEmpty());
            });
    connect(addButton, &QPushButton::clicked, this, [this, grpFiles] {
        QString initialDirectory;
        if (grpFiles->count() > 0) {
            initialDirectory = QFileInfo(grpFiles->item(0)->text()).absolutePath();
        }

        const QStringList paths = QFileDialog::getOpenFileNames(
            this, "Add GRP Files", initialDirectory,
            "GRP files (*.grp *.GRP);;All files (*)");
        for (const QString &path : paths) {
            if (grpFiles->findItems(path, Qt::MatchExactly).isEmpty()) {
                grpFiles->addItem(path);
            }
        }

        QStringList savedPaths;
        for (int index = 0; index < grpFiles->count(); ++index) {
            savedPaths.append(grpFiles->item(index)->text());
        }
        QSettings().setValue(grpFilesSettingsKey, savedPaths);
    });
    connect(removeButton, &QPushButton::clicked, this, [grpFiles] {
        qDeleteAll(grpFiles->selectedItems());

        QStringList savedPaths;
        for (int index = 0; index < grpFiles->count(); ++index) {
            savedPaths.append(grpFiles->item(index)->text());
        }
        QSettings().setValue(grpFilesSettingsKey, savedPaths);
    });

    categoryList->setCurrentRow(0);
}
