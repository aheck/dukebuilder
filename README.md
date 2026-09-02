# Duke Builder

A basic Qt 6 desktop application built with Meson. Conan supplies Qt and emits
the native file Meson uses to find it.

## Requirements

- Conan 2
- Meson
- Ninja
- A C++17 compiler

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
- **T:** enter Sprites mode. Drawing and selection are currently disabled in this mode.
- **Alt:** temporarily disable snapping while placing or previewing a point.

Drawing snaps to existing vertices before snapping to the grid. Sector fills are
shown when a chain is closed with three or more walls.

For a release build, use `Release` for Conan's `build_type`, use `release` for
Meson's `--buildtype`, and configure a separate build directory.
