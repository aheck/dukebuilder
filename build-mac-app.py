#!/usr/bin/env python3
"""Configure, build, and bundle a fresh macOS release build of Duke Builder.

Requires shared Qt 6 and its matching macdeployqt, Meson, and Xcode command
line tools. No game data is included. Run --help for packaging options.
"""

import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile


REPO = Path(__file__).resolve().parent
MACHO_MAGICS = {
    b'\xfe\xed\xfa\xce', b'\xce\xfa\xed\xfe',
    b'\xfe\xed\xfa\xcf', b'\xcf\xfa\xed\xfe',
    b'\xca\xfe\xba\xbe', b'\xbe\xba\xfe\xca',
    b'\xca\xfe\xba\xbf', b'\xbf\xba\xfe\xca',
}


def run(*command, capture=False):
    command = [str(part) for part in command]
    if not capture:
        print('+ ' + ' '.join(command), flush=True)
    result = subprocess.run(command, check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout if capture else None


def tool(name):
    found = shutil.which(name)
    if not found:
        raise ValueError(f'Required tool not found: {name}')
    return str(Path(found).absolute())


def introspect(meson, build, kind):
    return json.loads(run(meson, 'introspect', '--' + kind, build, capture=True))


def macho(path):
    with path.open('rb') as stream:
        return stream.read(4) in MACHO_MAGICS


def create_icon(destination):
    """Build the standard macOS 1x/2x icon representations from supplied PNGs."""
    iconutil = tool('iconutil')
    with tempfile.TemporaryDirectory(prefix='dukebuilder-icon-') as temporary:
        iconset = Path(temporary) / 'dukebuilder.iconset'
        iconset.mkdir()
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                pixels = size * scale
                suffix = '@2x' if scale == 2 else ''
                shutil.copy2(REPO / f'icons/icon-{pixels}x{pixels}.png',
                             iconset / f'icon_{size}x{size}{suffix}.png')
        run(iconutil, '--convert', 'icns', '--output', destination, iconset)


def load_commands(otool, path, arch):
    """Read load commands per slice, including paths containing spaces."""
    output = run(otool, '-arch', arch, '-l', path, capture=True)
    dependencies, rpaths = [], []
    for block in re.split(r'\nLoad command \d+\n', output):
        command = re.search(r'^\s*cmd (LC_\w+)\s*$', block, re.M)
        if not command:
            continue
        if command[1] == 'LC_RPATH':
            match = re.search(r'^\s*path (.+) \(offset \d+\)', block, re.M)
            if match:
                rpaths.append(match[1])
        elif command[1] in {'LC_LOAD_DYLIB', 'LC_LOAD_WEAK_DYLIB',
                            'LC_REEXPORT_DYLIB', 'LC_LOAD_UPWARD_DYLIB'}:
            match = re.search(r'^\s*name (.+) \(offset \d+\)', block, re.M)
            if match:
                dependencies.append(match[1])
    return dependencies, rpaths


def reject_sanitizer_symbols(path):
    """Reject sanitizer instrumentation inherited from static dependencies."""
    symbols = run(tool('nm'), '-u', path, capture=True)
    if re.search(r'_{2,3}(?:asan|ubsan|tsan|msan|lsan)_', symbols):
        raise ValueError(f'Sanitizer instrumentation found in release artifact: {path}')


def complete_bundle_dependencies(app, otool, lipo, library_paths):
    """Copy and relocate dependencies that macdeployqt could not resolve.

    Homebrew plugin rpaths refer to their original installation layout. Resolve
    missing libraries against the original library roots, then use explicit
    bundle-relative load paths so no Homebrew installation is needed at runtime.
    """
    frameworks = app / 'Contents' / 'Frameworks'
    frameworks.mkdir(exist_ok=True)
    processed = set()
    library_paths = set(library_paths)
    while True:
        images = [path for path in app.rglob('*')
                  if path.is_file() and not path.is_symlink() and macho(path)
                  and path not in processed]
        if not images:
            break
        for image in images:
            processed.add(image)
            dependencies = set()
            for arch in run(lipo, '-archs', image, capture=True).split():
                loaded, _ = load_commands(otool, image, arch)
                dependencies.update(loaded)
            for dependency in sorted(dependencies):
                if dependency.startswith(('/System/Library/', '/usr/lib/')):
                    continue
                if dependency.startswith('@rpath/'):
                    relative = dependency[len('@rpath/'):]
                elif dependency.startswith('/'):
                    library_paths.add(str(Path(dependency).parent))
                    if '.framework/' in dependency:
                        prefix, suffix = dependency.split('.framework/', 1)
                        relative = Path(prefix).name + '.framework/' + suffix
                    else:
                        relative = Path(dependency).name
                elif dependency.startswith(('@loader_path/', '@executable_path/')):
                    # Existing relocated paths are checked by validate_bundle.
                    continue
                else:
                    raise ValueError(f'Unsupported dependency path: {dependency}')
                relative_path = Path(relative)
                if relative_path.is_absolute() or '..' in relative_path.parts:
                    raise ValueError(f'Unsafe dependency path: {dependency}')
                destination = frameworks / relative_path
                if not destination.is_file():
                    candidates = [Path(root) / relative_path for root in sorted(library_paths)]
                    if dependency.startswith('/'):
                        candidates.insert(0, Path(dependency))
                    source = next((path for path in candidates if path.is_file()), None)
                    if source is None:
                        raise ValueError(f'Cannot find dependency for {image}: {dependency}')
                    library_paths.add(str(source.parent))
                    library_paths.add(str(source.resolve().parent))
                    if '.framework/' in relative:
                        framework_name = relative_path.parts[0]
                        source_framework = next(parent for parent in source.parents
                                                if parent.name == framework_name)
                        target_framework = frameworks / framework_name
                        if target_framework.exists():
                            raise ValueError(f'Incomplete framework: {target_framework}')
                        shutil.copytree(source_framework, target_framework, symlinks=True)
                    else:
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copy2(source, destination)
                run(tool('install_name_tool'), '-change', dependency,
                    '@executable_path/../Frameworks/' + relative, image)


def sign_bundle(app, codesign, identity):
    """Sign modified libraries, framework containers, and finally the app."""
    options = ['--force', '--sign', identity or '-']
    if identity:
        options += ['--options', 'runtime', '--timestamp']
    images = [path for path in app.rglob('*')
              if path.is_file() and not path.is_symlink() and macho(path)]
    for image in sorted(images):
        run(codesign, *options, image)
    for framework in sorted(app.rglob('*.framework'), key=lambda path: len(path.parts), reverse=True):
        if framework.is_dir() and not framework.is_symlink():
            run(codesign, *options, framework)
    run(codesign, *options, app)


def validate_bundle(app, executable, otool, lipo):
    """Reject missing slices and dependencies left outside the staged bundle.

    Resolve each image's rpaths and the executable's inherited rpaths. More
    elaborate loader chains are intentionally rejected instead of guessing.
    System libraries can live in dyld's cache and need not exist on disk.
    """
    arches = set(run(lipo, '-archs', executable, capture=True).split())
    if not arches or not arches <= {'arm64', 'x86_64'}:
        raise ValueError(f'Unsupported executable architectures: {sorted(arches)}')
    app = app.resolve()

    def expand(value, image):
        for prefix, base in (('@loader_path', image.parent),
                             ('@executable_path', executable.parent)):
            if value == prefix or value.startswith(prefix + '/'):
                return base / value[len(prefix):].lstrip('/')
        return Path(value) if value.startswith('/') else None

    for path in app.rglob('*'):
        if path.is_symlink():
            if not path.resolve().is_relative_to(app) or not path.exists():
                raise ValueError(f'Broken or external bundle symlink: {path}')
            continue
        if not path.is_file() or not macho(path):
            continue
        reject_sanitizer_symbols(path)
        slices = set(run(lipo, '-archs', path, capture=True).split())
        if not arches <= slices:
            raise ValueError(f'{path}: missing architectures {sorted(arches - slices)}')
        for arch in sorted(arches):
            dependencies, rpaths = load_commands(otool, path, arch)
            _, main_rpaths = load_commands(otool, executable, arch)
            search = [expand(p, path) for p in rpaths]
            search += [expand(p, executable) for p in main_rpaths]
            for dependency in dependencies:
                if dependency.startswith(('/System/Library/', '/usr/lib/')):
                    continue
                if dependency.startswith('@rpath/'):
                    candidates = [p / dependency[len('@rpath/'):] for p in search if p]
                else:
                    candidates = [expand(dependency, path)]
                if not any(p and p.is_file() and p.resolve().is_relative_to(app)
                           for p in candidates):
                    raise ValueError(f'{path} ({arch}): dependency is not bundled: {dependency}')
    return arches


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build_dir', nargs='?', type=Path, default=REPO / 'build-mac',
                        help='Build directory to delete and recreate (default: build-mac)')
    parser.add_argument('--native-file', action='append', type=Path, default=[],
                        help='Meson native file outside the build directory; may be repeated')
    parser.add_argument('--output-dir', type=Path, default=REPO / 'dist')
    parser.add_argument('--meson', default='meson')
    parser.add_argument('--macdeployqt', default='macdeployqt',
                        help='Deployment tool from the Qt installation used for this build')
    parser.add_argument('--bundle-id', default='org.dukebuilder.DukeBuilder')
    parser.add_argument('--icon', type=Path, help='Override the bundled artwork with a custom .icns icon')
    parser.add_argument('--licenses-dir', type=Path,
                        help='Directory of additional dependency redistribution notices')
    parser.add_argument('--sign-identity',
                        help='Developer ID identity; enables hardened runtime and timestamp')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        raise ValueError('Run this script on macOS with a native macOS build.')
    if not re.fullmatch(r'[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+', args.bundle_id):
        raise ValueError('--bundle-id must be a reverse-DNS identifier.')
    meson, deploy, otool, lipo, codesign = map(tool, (
        args.meson, args.macdeployqt, 'otool', 'lipo', 'codesign'))
    build_path = args.build_dir.absolute()
    build = build_path.resolve()
    # Only remove a dedicated build directory directly inside this repository.
    if build_path.is_symlink() or build.parent != REPO or build.name in {
        '.git', 'src', 'tests', 'scripts', 'packaging', 'icons', 'pics',
        'tutorials', 'dist',
    }:
        raise ValueError('Use a dedicated build directory directly inside the repository.')
    if build.exists() and (not build.is_dir() or (
            build.name != 'build-mac' and not (build / 'meson-private/coredata.dat').is_file())):
        raise ValueError('Refusing to remove a directory that is not a Meson build.')
    native_files = [path.resolve() for path in args.native_file]
    for native_file in native_files:
        if not native_file.is_file() or native_file.is_relative_to(build):
            raise ValueError(f'Native file must exist outside the build directory: {native_file}')
    # Build libduke separately so development archives with ASan cannot leak in.
    libduke_root = REPO.parent / 'libduke'
    libduke_build = libduke_root / 'build-mac-release'
    if libduke_build.is_symlink():
        raise ValueError('The libduke release build directory must not be a symlink.')
    libduke_setup = [meson, 'setup', libduke_build, libduke_root,
                     '--buildtype=release', '-Db_sanitize=none',
                     '-Drenderer=enabled', '-Dviewer=disabled', '-Dcheck_tests=false']
    if (libduke_build / 'meson-private/coredata.dat').is_file():
        libduke_setup.append('--reconfigure')
    for native_file in native_files:
        if native_file.is_relative_to(libduke_build):
            raise ValueError('Native files must be outside the libduke release build.')
        libduke_setup.extend(['--native-file', native_file])
    run(*libduke_setup)
    run(meson, 'compile', '-C', libduke_build)
    for archive in ('libduke.a', 'libduke-render.a'):
        reject_sanitizer_symbols(libduke_build / archive)
    # Discard stale configuration and enforce release settings for packaging.
    if build.exists():
        shutil.rmtree(build)
    setup = [meson, 'setup', build, REPO, '--buildtype=release', '-Db_sanitize=none',
             '-Dlibduke_build_dir=build-mac-release']
    for native_file in native_files:
        setup.extend(['--native-file', native_file])
    run(*setup)
    options = {entry['name']: entry['value']
               for entry in introspect(meson, build, 'buildoptions')}
    if options.get('buildtype') != 'release':
        raise ValueError('Configure Meson with --buildtype=release before packaging.')
    if options.get('b_sanitize') not in ('none', [], ['none']):
        raise ValueError('Configure Meson with -Db_sanitize=none before packaging.')
    machines = introspect(meson, build, 'machines')
    if machines['host']['system'] != 'darwin':
        raise ValueError('The Meson build must target macOS.')
    run(meson, 'compile', '-C', build, 'dukebuilder')
    # Query after compiling so Meson's automatic regeneration has run.
    project = introspect(meson, build, 'projectinfo')
    if project['descriptive_name'] != 'duke-builder':
        raise ValueError('The build directory is not a Duke Builder project.')
    version = project['version']
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)(?:[-+][A-Za-z0-9.+-]+)?', version)
    if not match:
        raise ValueError(f'Meson version must be major.minor.patch with an optional suffix: {version}')
    numeric_version = '.'.join(str(int(part)) for part in match.groups())
    targets = [t for t in introspect(meson, build, 'targets')
               if t['name'] == 'dukebuilder' and t['type'] == 'executable']
    if len(targets) != 1 or len(targets[0]['filename']) != 1:
        raise ValueError('Expected one dukebuilder executable in the Meson targets.')
    source = Path(targets[0]['filename'][0])
    if not source.is_absolute():
        source = build / source
    if not macho(source):
        raise ValueError(f'Expected a Mach-O executable: {source}')
    reject_sanitizer_symbols(source)
    linked = run(otool, '-L', source, capture=True)
    if not re.search(r'(?:QtCore\.framework/|libQt6Core[.])', linked):
        raise ValueError('This packager requires shared Qt 6. Static Qt needs Cocoa plugin integration in the build.')
    # Homebrew plugins use relative rpaths that stop working after relocation.
    # Give macdeployqt the original library roots so it can copy their dependencies.
    deployment_paths = set()
    for line in linked.splitlines()[1:]:
        dependency = line.strip().split(' (', 1)[0]
        if dependency.startswith('/') and not dependency.startswith(('/System/Library/', '/usr/lib/')):
            if '.framework/' in dependency:
                root = Path(dependency.split('.framework/', 1)[0]).parent
            else:
                root = Path(dependency).parent
            deployment_paths.add(str(root))
    if re.search(r'libduke[^/\s]*\.dylib', linked):
        raise ValueError('Build libduke and its renderer as static libraries.')
    if args.icon:
        with args.icon.open('rb') as stream:
            if stream.read(4) != b'icns':
                raise ValueError('--icon must be an Apple .icns file.')
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    # Stage on the destination filesystem. Never remove an existing release.
    with tempfile.TemporaryDirectory(prefix='.mac-app-', dir=output) as temporary:
        app = Path(temporary) / 'Duke Builder.app'
        contents = app / 'Contents'
        resources = contents / 'Resources'
        resources.mkdir(parents=True)
        executable = contents / 'MacOS' / 'dukebuilder'
        executable.parent.mkdir()
        shutil.copy2(source, executable)
        executable.chmod(executable.stat().st_mode | 0o111)
        shutil.copy2(REPO / 'LICENSE', resources / 'LICENSE')
        if args.licenses_dir:
            shutil.copytree(args.licenses_dir, resources / 'licenses')
        info = {
            'CFBundleDevelopmentRegion': 'en',
            'CFBundleExecutable': 'dukebuilder',
            'CFBundleIdentifier': args.bundle_id,
            'CFBundleInfoDictionaryVersion': '6.0',
            'CFBundleName': 'Duke Builder',
            'CFBundleDisplayName': 'Duke Builder',
            'CFBundlePackageType': 'APPL',
            'CFBundleShortVersionString': numeric_version,
            'CFBundleVersion': numeric_version,
            'DukeBuilderProjectVersion': version,
            'NSHighResolutionCapable': True,
            'LSApplicationCategoryType': 'public.app-category.developer-tools',
        }
        if args.icon:
            shutil.copy2(args.icon, resources / 'dukebuilder.icns')
        else:
            create_icon(resources / 'dukebuilder.icns')
        info['CFBundleIconFile'] = 'dukebuilder.icns'
        with (contents / 'Info.plist').open('wb') as stream:
            plistlib.dump(info, stream)
        (contents / 'PkgInfo').write_bytes(b'APPL????')
        (resources / 'qt.conf').write_text('[Paths]\nPlugins = PlugIns\n', encoding='utf-8')
        signing = (f'-sign-for-notarization={args.sign_identity}'
                   if args.sign_identity else '-codesign=-')
        library_paths = [f'-libpath={path}' for path in sorted(deployment_paths)]
        run(deploy, app, '-verbose=2', *library_paths, signing)
        for plugin in ('platforms/libqcocoa.dylib', 'imageformats/libqjpeg.dylib'):
            if not (contents / 'PlugIns' / plugin).is_file():
                raise ValueError(f'macdeployqt did not deploy required plugin: {plugin}')
        complete_bundle_dependencies(app, otool, lipo, sorted(deployment_paths))
        arches = validate_bundle(app, executable, otool, lipo)
        sign_bundle(app, codesign, args.sign_identity)
        run(codesign, '--verify', '--deep', '--strict', app)
        architecture = 'universal2' if len(arches) == 2 else next(iter(arches))
        destination = output / f'DukeBuilder-{version}-{architecture}.app'
        if os.path.lexists(destination):
            raise ValueError(f'Output already exists: {destination}. Choose another --output-dir or move it first.')
        app.rename(destination)
    print(f'Created {destination}')
    if not args.sign_identity:
        print('The bundle is ad-hoc signed for local use; it is not notarized.')
    else:
        print('Developer ID signing completed; notarization must be performed separately.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        sys.exit(f'Error: {error}')
