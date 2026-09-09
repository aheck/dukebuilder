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
./build/dukebuilder
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
- **Zoom:** the default 100% is a level-design working scale (1024 map units
  span about 82 pixels). Use the status-bar zoom selector to return to 100%.
- **[ / ]:** decrease or increase the snapping grid size, also selectable in the
  status bar. Sizes range from 1 to 4096 map units; the default is 256 (about
  20 pixels at 100% zoom). The drawn grid uses this same spacing at every zoom;
  every eighth line is brighter for orientation.
- **G or the Grid toolbar button:** show or hide the grid.
- **F11 / View → Reorient Grid to Line:** with one line selected, rotate the
  nearest grid axis parallel to it. Drawing and vertex-insertion snapping follow
  the rotated grid; its origin and spacing stay unchanged.
- **F12 / View → Reset Grid Orientation:** restore the default grid axes.
- **T:** enter Sprites mode.
- **Lines mode:** select one or more lines and press Delete to remove them.
  Connected selected lines collapse onto their lowest-numbered endpoint, keeping
  the remaining sector boundaries closed. Surviving sectors retain their
  properties and relative order. Sectors collapsed below three edges disappear.
- **Vertices mode:** hover over a line to preview a new vertex; double-click to
  split the line there. The preview snaps to grid crossings along the line;
  hold Alt for a free position on the line. Shared walls split on both sides.
  Right-drag selected vertices to move them: the grabbed vertex snaps to the
  active grid, including its rotation. Hold Alt to move freely. Multiple selected
  vertices move together without changing their relative positions.
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

The Texture Browser has a category sidebar, including **All**, **Used in this
map**, and **Others** for unmapped tiles. Search accepts tile numbers, original
tile names, and aliases such as "pistol" or "pig cop"; multiple words narrow
the results together with the selected category. Double-click a tile to select it.
Used textures refresh whenever the browser opens and include both wall sides,
active overlays, floors, ceilings, and sprites.

The catalog uses the original Atomic Edition
[tile definitions](https://github.com/videogamepreservation/dukenukem3d/blob/master/source/NAMES.H),
with categories and animation-family names added by the editor. Surface categories
also include tile IDs used on walls, floors, and ceilings in the 41 shipped
Atomic Edition maps, covering unnamed building materials. This metadata is
bundled; no game archive is needed to build the catalog. Original tiles
without a known name or family remain searchable by number in Others. Custom
GRPs may replace the artwork at those IDs, so names describe the original game.

To add a neighboring sector, draw from existing boundary vertices and reuse a
complete boundary edge, or finish an open chain between existing vertices with
Enter. Boundaries shared by two sectors automatically become two-sided walls
and appear red; outer boundaries remain light gray. Shared vertices move both
sides together. Hover and selection highlighting still apply to either kind of
wall. Joining partway along an existing edge requires splitting that edge, which
is not yet supported.

Drawing a closed sector entirely inside another sector creates an inner loop
in the surrounding room and two-sided walls connecting the sectors. Raise the
inner floor (use a smaller Z value) to make a box or platform. Maps saved by
older versions with disconnected overlapping sectors need their topology
repaired; opening them does not automatically join intentional overlaps.

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

**File → Recent Files** lists the ten most recently opened or saved maps,
newest first, and remembers them between sessions. Select a path to reopen it,
or use **Clear Recent Files** to clear the list. Reopening a recent map uses
the same unsaved-changes prompt as Open.

Imported maps retain sector loops, independent portal-side properties, sprites,
and player start information. Connected inner sectors remain editable after
reopening. Overlapping single-loop sectors support line deletion without
rebuilding their faces; adding lines remains restricted. Imported void loops
and effect sectors still restrict structural edits. Saving applies the validation rules
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
