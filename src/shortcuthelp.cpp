#include "shortcuthelp.h"
#include <QDialog>
#include <QDialogButtonBox>
#include <QKeySequence>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace {
struct Shortcut { QString keys; QString action; };
QString section(const QString &title, std::initializer_list<Shortcut> rows)
{
    QString html = "<h2>" + title.toHtmlEscaped() + "</h2><table width='100%' cellspacing='0' cellpadding='6'>";
    for (const auto &row : rows)
        html += "<tr><td width='35%' valign='top'><b>" + row.keys.toHtmlEscaped()
            + "</b></td><td>" + row.action.toHtmlEscaped() + "</td></tr>";
    return html + "</table>";
}
QString standard(QKeySequence::StandardKey key)
{
    return QKeySequence(key).toString(QKeySequence::NativeText);
}
QString content(bool threeD)
{
    QString html = "<html><body><h1>" + QString(threeD ? "3D Mode Shortcuts" : "2D Mode Shortcuts")
        + "</h1><p>Shortcuts apply when the map viewport has focus. This window can stay open while you edit.</p>";
    if (threeD) {
        html += section("Navigation", {
            {"W / S", "Fly forward/backward along the viewing direction, including pitch."},
            {"A / D", "Strafe left/right. A aligns textures instead when a multi-selection includes walls."}, {"Shift", "Move faster."},
            {"Mouse", "Look around while the mouse is captured."},
            {"Escape", "Clear the selection while keeping mouse look active. With no selection, release the mouse."}, {"Left click in viewport", "Resume mouse look if released. While mouse look is active, select the highlighted surface or sprite; click it again or click empty space to clear selection."},
            {"Shift + left click", "Add or remove a surface or sprite from the selection. A plain click replaces the selection. Returning to 2D clears it."},
            {"Q", "Return to 2D mode."}, {"H", "Toggle surface and sprite highlighting."}
        });
        html += section("Heights and slopes", {
            {"Mouse wheel", "Raise/lower the selected (or otherwise highlighted) floor, ceiling, or sprite by 1024 Z units per notch. Wheel-up raises it."},
            {"Shift + wheel", "Raise/lower by a finer 128 Z units per notch."},
            {"Ctrl + wheel", "Adjust all selected surfaces and sprites (or the highlight when nothing is selected) by 1 shade unit per notch. Wheel-up darkens; wheel-down brightens. Each batch is one undoable edit."},
            {"Alt + wheel", "Change the selected (or otherwise highlighted) floor/ceiling slope by 256 per notch."},
            {"Shift + Alt + wheel", "Change slope by a finer 16 per notch."},
            {"First wall (2D properties)", "Choose the slope axis in 2D. A nonzero slope enables the slope flag; returning to zero clears it."}
        });
        html += section("Textures and sprites", {
            {"Right click", "Choose one texture for all multi-selected objects, or the highlighted object otherwise."},
            {"Arrow keys", "Pan multi-selected wall, floor, and ceiling textures, or the highlighted surface otherwise."},
            {"Shift + arrows", "Resize its texture. Right/Up enlarges; Left/Down shrinks. Floors and ceilings support two uniform sizes."},
            {"Ctrl+C / Ctrl+V", "Copy/paste the highlighted wall, floor, or ceiling texture tile. Other properties stay unchanged."},
            {"R", "Reset the highlighted wall side to the default texture scale."},
            {"A (wall multi-selection)", "Align connected selected walls to the pointed-at selected wall, preserving texture scale. Different textures and incompatible sides are skipped. One undo restores the batch."},
            {"O", "Stick the highlighted sprite to the nearest wall of its sector. Position and alignment change; height and tags are retained."}
        });
        html += "<p>Wheel edits keep targeting the orange selection when you look away. Multi-selection supports shade, height, slope, and texture edits as one undoable batch. Heights skip walls; slopes affect only floors and ceilings; surface panning/scaling skip sprites. Ctrl+C samples the highlight; Ctrl+V applies the copied tile to the multi-selection. "
                "This is a free-flight preview without collision or game simulation.</p>";
    } else {
        html += section("Modes and view", {
            {"Ctrl+D", "Draw mode."}, {"L", "Lines mode."}, {"V", "Vertices mode."},
            {"S", "Sectors mode."}, {"T", "Sprites mode."},
            {"Shift+T", "Show/hide sprites in 2D. Sprites mode always shows sprites."},
            {"Q", "Enter 3D near the pointer (or the viewport center when the pointer is outside)."},
            {"Middle-mouse drag", "Pan the map."}, {"Mouse wheel", "Zoom around the pointer."},
            {"G", "Show/hide the grid."}, {"[ / ]", "Decrease/increase grid spacing."},
            {"F11", "Reorient the grid to the selected line; select exactly one line first."},
            {"F12", "Reset the grid orientation."}
        });
        html += section("Drawing", {
            {"Left click", "Place a connected wall vertex."},
            {"Click the first vertex", "Close the shape and create a sector (at least three vertices)."},
            {"Click an existing wall", "Connect to the wall; finish automatically when the chain forms a sector with existing walls."},
            {"Right click / Enter", "Finish the line chain. It is retained when it forms a sector with existing walls."},
            {"Backspace", "Remove the last point from the unfinished drawing."},
            {"Escape", "Cancel the unfinished drawing."},
            {"Alt", "Temporarily disable grid and wall snapping while placing or moving points."}
        });
        html += section("Selection and geometry", {
            {"Left click / left-drag rectangle", "Select objects in the active selection mode."},
            {"Shift + left click", "Add/remove an object from the selection."},
            {"Right-drag selection", "Move selected vertices, lines, sectors, sprites, or the player start. Hold Alt to move freely."},
            {"Double-click shadow vertex", "In Vertices mode, split the hovered line at the preview point. Grid snapping applies; Alt allows free placement."},
            {"Delete", "Delete selected vertices, lines, sectors, or sprites. Deleting an inner sector leaves a hole; the player start cannot be deleted."},
            {"J", "Join selected adjacent sectors. Select the donor sector first, then Shift-click the others; its properties are retained."}
        });
        html += section("Sprites", {
            {"Ctrl+C / Ctrl+V", "Copy selected sprites and paste duplicates one grid cell down and right. Requires Sprites mode."},
            {"Right click empty space", "Create a sprite in Sprites mode."},
            {"Right click a sprite", "Choose its texture in Sprites mode."},
            {"O", "Stick one selected sprite to the nearest wall of its sector. Requires Sprites mode; preserves height and tags."},
            {"Player-start arrow", "Select and right-drag in Sprites mode. It has no selectable texture."}
        });
    }
    html += section("Files, history, and help", {
        {standard(QKeySequence::New), "New map."}, {standard(QKeySequence::Open), "Open map."},
        {standard(QKeySequence::Save), "Save map."}, {standard(QKeySequence::SaveAs), "Save map as a new file."},
        {standard(QKeySequence::Undo), "Undo the last edit (an unfinished drawing is cancelled first)."},
        {"Ctrl+Y / Ctrl+Shift+Z", "Redo."},
        {"F4", "Check Map using save validation. Show in Map selects the first reported error in 2D."},
        {"F9", "Save and run the map in EDuke32. Configure its executable in Settings first."},
        {"F1", "Open the current mode's shortcut reference."},
        {standard(QKeySequence::Quit), "Quit; unsaved work prompts for confirmation."}
    });
    return html + "</body></html>";
}
}

QDialog *createShortcutHelp(QWidget *parent, bool threeDimensional)
{
    auto *dialog = new QDialog(parent, Qt::Window);
    dialog->setObjectName(threeDimensional ? "shortcuts3D" : "shortcuts2D");
    dialog->setWindowTitle(threeDimensional ? "3D Mode Shortcuts" : "2D Mode Shortcuts");
    dialog->setModal(false);
    dialog->resize(600, 650);
    auto *layout = new QVBoxLayout(dialog);
    auto *browser = new QTextBrowser(dialog);
    browser->setOpenLinks(false);
    browser->setOpenExternalLinks(false);
    browser->document()->setDefaultStyleSheet("h1 { font-size: 20pt; } h2 { font-size: 13pt; margin-top: 18px; } td { padding: 6px; }");
    browser->setHtml(content(threeDimensional));
    layout->addWidget(browser);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    return dialog;
}
