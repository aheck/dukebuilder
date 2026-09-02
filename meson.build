project(
  'duke-builder',
  'cpp',
  version: '0.1.0',
  default_options: [
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

qt6 = dependency('qt6', modules: ['Core', 'Widgets'])
cpp = meson.get_compiler('cpp')

# Qt's static Conan package uses the system BLAKE2 implementation, but its
# generated pkg-config metadata does not currently propagate this dependency.
libb2 = cpp.find_library('b2', required: true)
app_dependencies = [qt6, libb2]
app_cpp_args = []

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
  ],
  cpp_args: app_cpp_args,
  dependencies: app_dependencies,
  install: true,
)
