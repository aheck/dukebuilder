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

qt6 = dependency('qt6', modules: ['Core', 'Widgets'])
cpp = meson.get_compiler('cpp')

libduke_root = meson.current_source_dir() / '..' / 'libduke'
libduke = cpp.find_library(
  'duke',
  dirs: libduke_root / 'build',
  static: true,
  required: true,
)
libduke_inc = include_directories('../libduke/include')

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

if host_machine.system() == 'linux'
  xcb_plugin = dependency('qt6-qtQXcbIntegrationPlugin')
  app_dependencies += xcb_plugin
  app_cpp_args += '-DDUKE_BUILDER_STATIC_XCB_PLUGIN'
endif

executable(
  'duke-builder',
  [
    'src/main.cpp',
    'src/mainwindow.cpp',
    'src/mainwindow.h',
    'src/mapdocument.cpp',
    'src/mapdocument.h',
    'src/mapeditor.cpp',
    'src/mapeditor.h',
    'src/settingsdialog.cpp',
    'src/settingsdialog.h',
    'src/texturebrowserwidget.cpp',
    'src/texturebrowserwidget.h',
    'src/texturebrowserwindow.cpp',
    'src/texturebrowserwindow.h',
  ],
  cpp_args: app_cpp_args,
  dependencies: app_dependencies,
  include_directories: libduke_inc,
  install: true,
)
