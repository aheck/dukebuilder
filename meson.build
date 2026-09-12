project(
  'duke-builder',
  'cpp',
  version: '0.1.0',
  default_options: [
    'b_sanitize=address',
    'b_lundef=false',
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

qt6 = dependency('qt6', modules: ['Core', 'Widgets', 'OpenGLWidgets'])
cpp = meson.get_compiler('cpp')

libduke_root = meson.current_source_dir() / '..' / 'libduke'
libduke = cpp.find_library(
  'duke',
  dirs: libduke_root / 'build-render',
  static: true,
  required: true,
)
libduke_inc = include_directories('../libduke/include', '../libduke/third_party/sokol')
libduke_render = cpp.find_library('duke-render', dirs: libduke_root / 'build-render', static: true, required: true)
renderer_dependencies = [libduke_render, libduke, dependency('threads')]
if host_machine.system() == 'linux'
  renderer_dependencies += [dependency('gl'), cpp.find_library('dl')]
elif host_machine.system() == 'windows'
  renderer_dependencies += cpp.find_library('opengl32')
elif host_machine.system() == 'darwin'
  renderer_dependencies += dependency('appleframeworks', modules: ['OpenGL'])
endif

# Qt's static Conan package uses the system BLAKE2 implementation, but its
# generated pkg-config metadata does not currently propagate this dependency.
libb2 = cpp.find_library('b2', required: true)
app_dependencies = [qt6, libb2, libduke]
app_cpp_args = []

mapdocument_test = executable(
  'mapdocument-test',
  ['tests/mapdocument_test.cpp', 'src/mapdocument.cpp'],
  include_directories: include_directories('src'),
  dependencies: [qt6, libb2],
)
test('mapdocument', mapdocument_test)

mapsave_test = executable(
  'mapsave-test',
  ['tests/mapsave_test.cpp', 'src/mapsave.cpp', 'src/mapload.cpp', 'src/mapdocument.cpp'],
  include_directories: [include_directories('src'), libduke_inc],
  dependencies: app_dependencies,
)
test('mapsave', mapsave_test)

texturecatalog_test = executable(
  'texturecatalog-test',
  ['tests/texturecatalog_test.cpp', 'src/texturecatalog.cpp', 'src/mapdocument.cpp'],
  include_directories: include_directories('src'),
  dependencies: [qt6, libb2],
)
test('texturecatalog', texturecatalog_test)

if host_machine.system() == 'linux'
  xcb_plugin = dependency('qt6-qtQXcbIntegrationPlugin')
  # Conan omits the static GLX integration plugin from its pkg-config metadata.
  qt_prefix = xcb_plugin.get_variable(pkgconfig: 'prefix')
  xcb_glx_plugin = cpp.find_library('qxcb-glx-integration',
    dirs: qt_prefix / 'plugins/xcbglintegrations', static: true, required: true)
  app_dependencies += [xcb_plugin, xcb_glx_plugin, dependency('xcb-glx')]
  app_cpp_args += '-DDUKE_BUILDER_STATIC_XCB_PLUGIN'
endif

app_sources = [
    'src/mapview3d.cpp',
    'src/previewcamera.cpp',
    'src/mainwindow.cpp',
    'src/mainwindow.h',
    'src/mapdocument.cpp',
    'src/mapdocument.h',
    'src/mapsave.cpp',
    'src/mapsave.h',
    'src/mapload.cpp',
    'src/mapeditor.cpp',
    'src/mapeditor.h',
    'src/settingsdialog.cpp',
    'src/settingsdialog.h',
    'src/texturebrowserwidget.cpp',
    'src/texturebrowserwidget.h',
    'src/texturecatalog.cpp',
    'src/texturecatalog.h',
    'src/texturebrowserwindow.cpp',
    'src/texturebrowserwindow.h',
  ]

executable(
  'dukebuilder',
  ['src/main.cpp'] + app_sources,
  cpp_args: app_cpp_args,
  dependencies: app_dependencies + renderer_dependencies,
  include_directories: libduke_inc,
  install: true,
  install_tag: 'runtime',
)

install_data('packaging/dukebuilder.desktop',
  install_dir: get_option('datadir') / 'applications', install_tag: 'runtime')
install_data('packaging/dukebuilder.svg',
  install_dir: get_option('datadir') / 'icons/hicolor/scalable/apps', install_tag: 'runtime')

previewcamera_test = executable('previewcamera-test',
  ['tests/previewcamera_test.cpp', 'src/previewcamera.cpp', 'src/mapdocument.cpp', 'src/mapsave.cpp'],
  include_directories: [include_directories('src'), libduke_inc],
  dependencies: [qt6, libb2, libduke])
test('previewcamera', previewcamera_test)

# Optional manual GPU smoke test: pass a DUKE3D.GRP path on a desktop display.
qt_test = dependency('qt6', modules: ['Test'])
executable('view3d-smoke',
  ['tests/view3d_smoke.cpp'] + app_sources,
  cpp_args: app_cpp_args,
  include_directories: [include_directories('src'), libduke_inc],
  dependencies: app_dependencies + renderer_dependencies + [qt_test])
