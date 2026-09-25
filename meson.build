project(
  'duke-builder',
  'cpp',
  version: '0.2.0',
  default_options: [
    # Sanitizers are opt-in; release artifacts must not require their runtimes.
    'b_sanitize=none',
    'b_lundef=false',
    'cpp_std=c++17',
    'warning_level=3',
  ],
)

qt6 = dependency('qt6', modules: ['Core', 'Widgets', 'OpenGLWidgets'])
cpp = meson.get_compiler('cpp')

libduke_root = meson.current_source_dir() / '..' / 'libduke'
fs = import('fs')
libduke_libraries = []
foreach name : ['duke', 'duke-render']
  archive = libduke_root / 'build' / ('lib' + name + '.a')
  if host_machine.system() == 'windows' and fs.is_file(archive)
    # Meson can produce .a static archives alongside .lib DLL import libraries
    # on Windows. Pass the archive explicitly: MSVC's library-name lookup uses
    # .lib, and picking the import library would silently enable dynamic linking.
    libduke_libraries += declare_dependency(link_args: [archive])
    message('Linking static libduke archive: ' + archive)
  else
    libduke_libraries += cpp.find_library(name,
      dirs: libduke_root / 'build', static: true, required: true)
  endif
endforeach
libduke = libduke_libraries[0]
libduke_render = libduke_libraries[1]
libduke_inc = include_directories('../libduke/include', '../libduke/third_party/sokol')
renderer_dependencies = [libduke_render, libduke, dependency('threads')]
if host_machine.system() == 'linux'
  renderer_dependencies += [dependency('gl'), cpp.find_library('dl')]
elif host_machine.system() == 'windows'
  renderer_dependencies += cpp.find_library('opengl32')
elif host_machine.system() == 'darwin'
  renderer_dependencies += dependency('appleframeworks', modules: ['OpenGL'])
endif

qt_dependencies = [qt6]
# Some Linux static Qt packages omit BLAKE2 from their dependency metadata.
# Probe the actual Qt configuration and link interface rather than requiring a
# system library on Windows, shared Qt, or correctly packaged static Qt builds.
if host_machine.system() == 'linux'
  qt_is_static = cpp.compiles('''
    #include <QtCore/qglobal.h>
    #ifndef QT_STATIC
    #error Qt is not static
    #endif
    int main() { return 0; }
  ''', dependencies: qt6, name: 'Qt is statically built')
  if qt_is_static
    qt_hash_probe = '''
      #include <QtCore/QCryptographicHash>
      #include <QtCore/QByteArray>
      int main() {
        return QCryptographicHash::hash(QByteArray("probe"),
            QCryptographicHash::Blake2b_512).isEmpty();
      }
    '''
    if not cpp.links(qt_hash_probe, dependencies: qt_dependencies,
                     name: 'Qt hashing links with declared dependencies')
      libb2 = cpp.find_library('b2', required: false)
      if not libb2.found()
        error('Static Qt hashing cannot link. Check Qt dependency metadata; this Linux Qt package may require the libb2 development package.')
      endif
      if not cpp.links(qt_hash_probe, dependencies: qt_dependencies + [libb2],
                       name: 'Qt hashing links with libb2 workaround')
        error('Static Qt hashing still cannot link with libb2. Check Qt dependency metadata and meson-logs/meson-log.txt for the missing dependencies.')
      endif
      qt_dependencies += [libb2]
      message('Applying Linux static Qt libb2 dependency workaround')
    endif
  endif
endif
app_dependencies = qt_dependencies + [libduke]
app_cpp_args = []

mapdocument_test = executable(
  'mapdocument-test',
  ['tests/mapdocument_test.cpp', 'src/mapdocument.cpp'],
  include_directories: include_directories('src'),
  dependencies: qt_dependencies,
)
test('mapdocument', mapdocument_test)

mapsave_test = executable(
  'mapsave-test',
  ['tests/mapsave_test.cpp', 'src/mapsave.cpp', 'src/mapload.cpp', 'src/mapdocument.cpp'],
  include_directories: [include_directories('src'), libduke_inc],
  dependencies: app_dependencies,
)
test('mapsave', mapsave_test)

grp_test_dependencies = app_dependencies
grp_test_args = []
if cpp.compiles('''
    #include <QtCore/qglobal.h>
    #ifndef QT_STATIC
    #error Qt is not static
    #endif
    int main() { return 0; }
  ''', dependencies: qt6, name: 'GRP tests use static Qt')
  qt_test_prefix = dependency('Qt6Core').get_variable(pkgconfig: 'prefix')
  grp_test_dependencies += cpp.find_library('qminimal',
    dirs: qt_test_prefix / 'plugins/platforms', static: true)
  grp_test_args += '-DDUKE_BUILDER_STATIC_MINIMAL_PLUGIN'
endif
grpfilemanager_test = executable('grpfilemanager-test',
  ['tests/grpfilemanager_test.cpp', 'src/grpfilemanagerwindow.cpp'],
  include_directories: [include_directories('src'), libduke_inc],
  cpp_args: grp_test_args,
  dependencies: grp_test_dependencies)
test('grpfilemanager', grpfilemanager_test, env: ['QT_QPA_PLATFORM=minimal'])

mapeditor_drawing_test = executable('mapeditor-drawing-test',
  ['tests/mapeditor_drawing_test.cpp', 'src/mapeditor.cpp', 'src/mapdocument.cpp',
   'src/mapsave.cpp', 'src/mapload.cpp'],
  include_directories: [include_directories('src'), libduke_inc],
  cpp_args: grp_test_args,
  dependencies: grp_test_dependencies)
test('mapeditor-drawing', mapeditor_drawing_test, env: ['QT_QPA_PLATFORM=minimal'])

texturecatalog_test = executable(
  'texturecatalog-test',
  ['tests/texturecatalog_test.cpp', 'src/texturecatalog.cpp', 'src/mapdocument.cpp'],
  include_directories: include_directories('src'),
  dependencies: qt_dependencies,
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
    'src/walltexturealignment.cpp',
    'src/shortcuthelp.cpp',
    'src/recovery.cpp',
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
    'src/grpfilemanagerwindow.cpp',
    'src/grpfilemanagerwindow.h',
  ]

# Qt supplies the Windows GUI entry point that forwards to main(). Keep this
# dependency on the application only; test executables retain their consoles.
gui_entrypoint = []
if host_machine.system() == 'windows'
  gui_entrypoint += dependency('qt6', modules: ['Core'], main: true)
endif

executable(
  'dukebuilder',
  ['src/main.cpp'] + app_sources,
  cpp_args: app_cpp_args,
  dependencies: app_dependencies + renderer_dependencies + gui_entrypoint,
  win_subsystem: 'windows',
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
  dependencies: qt_dependencies + [libduke])
test('previewcamera', previewcamera_test)

walltexturealignment_test = executable('walltexturealignment-test',
  ['tests/walltexturealignment_test.cpp', 'src/walltexturealignment.cpp', 'src/mapdocument.cpp'],
  include_directories: include_directories('src'),
  dependencies: qt_dependencies)
test('walltexturealignment', walltexturealignment_test)

# Optional manual GPU smoke test: pass a DUKE3D.GRP path on a desktop display.
qt_test = dependency('qt6', modules: ['Test'])
executable('view3d-smoke',
  ['tests/view3d_smoke.cpp'] + app_sources,
  cpp_args: app_cpp_args,
  include_directories: [include_directories('src'), libduke_inc],
  dependencies: app_dependencies + renderer_dependencies + [qt_test])

recovery_test = executable('recovery-test',
  ['tests/recovery_test.cpp', 'src/recovery.cpp', 'src/mapdocument.cpp'],
  include_directories: include_directories('src'),
  dependencies: qt_dependencies)
test('recovery', recovery_test)

spritelotags_test = executable('spritelotags-test', 'tests/spritelotags_test.cpp',
  include_directories: include_directories('src'))
test('spritelotags', spritelotags_test)
