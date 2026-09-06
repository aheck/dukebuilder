# Duke Builder

A basic Qt 6 desktop application built with Meson. Conan supplies Qt and emits
the native file Meson uses to find it.

## Requirements

- Conan 2
- Meson
- Ninja
- A C++17 compiler
- A configured and built `../libduke` checkout

Create a Conan profile once if you do not already have one:

```sh
conan profile detect
```

Install the dependencies and generate the Meson native file:

```sh
conan install . --output-folder=conan --build=missing -s build_type=Debug
```

Configure and build the application:

```sh
meson setup build --native-file conan/conan_meson_native.ini --buildtype debug
meson compile -C build
```

Run it:

```sh
./build/duke-builder
```

## 2D editor controls

- **Left click:** place connected wall vertices. Lines can be drawn at any angle.
- **Click the first vertex:** close the current shape and create a sector.
- **Right click or Enter:** finish the current line chain. It is kept only if its
  walls, together with existing walls, create a sector.
- **Backspace:** remove the last point from the active drawing.
- **Escape:** cancel the active drawing.
- **Middle-mouse drag:** pan the map.
- **Mouse wheel:** zoom around the cursor.
- **[ / ]:** decrease or increase the snapping grid size.
- **G or the Grid toolbar button:** show or hide the grid.
- **T:** enter Sprites mode.
- **Lines mode:** select one or more lines and press Delete to remove them.
  Connected selected lines collapse onto their lowest-numbered endpoint, keeping
  the remaining sector boundaries closed. Surviving sectors retain their
  properties and relative order. Sectors collapsed below three edges disappear.
- **Sprites mode:** right-click empty space to add a sprite; left-click or
  rubber-band to select; right-drag to move; right-click a sprite to choose its
  texture; Delete removes selected sprites.
- **Player start:** the permanent arrow can be selected and right-dragged only
  in Sprites mode; it has no selectable texture and is omitted from selections
  containing ordinary sprites.
- **Alt:** temporarily disable snapping while placing or previewing a point.

Drawing snaps to existing vertices before snapping to the grid. Sector fills are
shown when a chain is closed with three or more walls.

The Editor toolbar's **Floor textures** and **Ceiling textures** buttons tile
the corresponding images inside sectors. **Plain fill** restores the original
untextured appearance (the default). Missing textures use the plain fill.
These are 2D previews; selection and hover highlighting remain visible.

To add a neighboring sector, draw from existing boundary vertices and reuse a
complete boundary edge, or finish an open chain between existing vertices with
Enter. Boundaries shared by two sectors automatically become two-sided walls
and appear red; outer boundaries remain light gray. Shared vertices move both
sides together. Hover and selection highlighting still apply to either kind of
wall. Joining partway along an existing edge requires splitting that edge, which
is not yet supported.

In Lines mode, select one wall to edit its texture, overlay texture, shade,
palette, texture repeat and panning, flags (`cstat`), hitag, and lotag. For a
two-sided wall, use the Properties panel's Side dropdown to choose Front or
Back, labeled with its sector number. The tick on the selected line points
toward the active side. Each side retains its own values; a one-sided wall
shows only its available side.

For a release build, use `Release` for Conan's `build_type`, use `release` for
Meson's `--buildtype`, and configure a separate build directory.

## Opening and saving maps

Use **File → Open Map** (Ctrl+O) to open a classic version-7 Build `.map`
file. The view centers on the player start, and Save uses the opened filename.
An unsuccessful open leaves the current map intact. New, Open, and closing the application prompt to
save unsaved changes first; choose Discard to proceed without saving or Cancel
to keep editing. A canceled or failed save stops the operation, including quitting.

Imported maps retain sector loops, independent portal-side properties, sprites,
and player start information. Maps with holes, overlapping geometry, or effect
sectors support vertex and property edits, but adding and deleting lines is
disabled to preserve their topology. Saving still applies the validation rules
below; some original effect geometry may be opened but cannot yet be saved.

Use **File → Save** (Ctrl+S) or **Save As** (Ctrl+Shift+S) to write a classic
version-7 Build `.map` file. Save remembers the filename until you start a new
map. Existing files are replaced only after validation and writing succeed.

Finish or cancel any active drawing before saving. The player start must lie
inside a closed sector, between its ceiling and floor. Sprites must have a
texture and lie inside a sector. Errors identify invalid placement, geometry,
field values, or format limits so they can be corrected in the editor.

Export preserves numeric X/Y/Z coordinates, rounds fractional coordinates to
integers, and converts degree angles to Build's 0–2047 angle range. Sector
numbers and first-wall selections are preserved; shared lines produce two
linked wall records with independent properties. Sprite and player sector
numbers are determined from the exported geometry, retaining imported sector
membership when it still matches the position. Ambiguous placement inside
overlapping sectors without a matching imported membership is rejected.

The classic limits are 1,024 sectors, 8,192 wall sides, 4,096 sprites, and texture
numbers 0–6,143, matching the original
[Duke 3D definitions](https://github.com/videogamepreservation/dukenukem3d/blob/master/source/BUILD.H).
Textures are referenced by tile number; their artwork is supplied by the game.

Configure the executable in **Settings → EDuke32**, then use
**Testing → Run in eDuke32** (F9) to test the current map. New or modified maps
are saved first; canceling or failing to save stops the launch. The executable
receives `-usecwd -nosetup -j <map directory> -map <map filename>` and runs with its own
directory as the working directory.

Run geometry and map open/save regression tests with `meson test -C build`.
