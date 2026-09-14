#!/usr/bin/env python3
"""Deploy Qt and package a Windows x64 application directory with NSIS 3."""
import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parent.parent


def nsis_string(value):
    """Quote a literal for NSIS, including paths containing spaces or dollars."""
    value = str(value)
    if any(c in value for c in '\r\n\0'):
        raise ValueError('Newlines and NULs are not supported in packaging paths.')
    return '"' + value.replace('$', '$$').replace('"', '$\\"') + '"'


def check_pe(path):
    """Reject Linux binaries, truncated headers and non-x64 PE images."""
    with path.open('rb') as f:
        header = f.read(64)
        if len(header) != 64 or header[:2] != b'MZ':
            raise ValueError(f'{path}: expected a Windows x64 PE file.')
        f.seek(struct.unpack_from('<I', header, 60)[0])
        pe = f.read(26)
        if len(pe) != 26 or pe[:4] != b'PE\0\0' or struct.unpack_from('<H', pe, 4)[0] != 0x8664 or struct.unpack_from('<H', pe, 24)[0] != 0x20b:
            raise ValueError(f'{path}: expected an x64 PE32+ executable or DLL.')


def payload_files(stage):
    if not (stage / 'dukebuilder.exe').is_file():
        raise ValueError('The staging directory must contain dukebuilder.exe at its root.')
    files = []
    names = set()
    for path in sorted(stage.rglob('*')):
        if path.is_symlink():
            raise ValueError(f'Symlinks are not supported: {path}')
        rel = path.relative_to(stage)
        for part in rel.parts:
            if any(c in part for c in '<>:"/\\|?*') or part.endswith((' ', '.')) or re.match(r'^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)', part, re.I):
                raise ValueError(f'Invalid Windows filename: {rel}')
        key = str(rel).casefold()
        if key in names:
            raise ValueError(f'Case-insensitive filename collision: {rel}')
        names.add(key)
        if not path.is_file():
            continue
        if path.suffix.lower() in {'.grp', '.map', '.art', '.pdb', '.lib', '.a', '.so'} or path.name.lower() in {'uninstall.exe', 'eduke32.exe'}:
            raise ValueError(f'Remove game data, build files or reserved files from staging: {rel}')
        if path.suffix.lower() in {'.exe', '.dll'}:
            check_pe(path)
        files.append(rel)
    return files


def make_config(stage, files, version, output, vc_redist=None):
    numeric = version.split('-')[0].split('+')[0].split('.')
    if len(numeric) != 3 or any(int(x) > 65535 for x in numeric):
        raise ValueError('Version must have three numeric components, each at most 65535.')
    lines = [f'!define APP_VERSION {nsis_string(version)}',
             f'!define NUMERIC_VERSION {nsis_string(".".join(numeric) + ".0")}',
             f'!define OUTPUT_FILE {nsis_string(output)}',
             f'!define INSTALLED_KIB {max(1, (sum((stage / p).stat().st_size for p in files) + 1023) // 1024)}',
             '!macro InstallPayload']
    directories = set()
    for rel in files:
        parent = str(rel.parent).replace('\\', '/')
        target = '$INSTDIR' if parent == '.' else '$INSTDIR\\' + parent.replace('/', '\\').replace('$', '$$')
        lines += [f'  SetOutPath "{target}"', f'  File {nsis_string(stage / rel)}']
        directories.update(p for p in rel.parents if str(p) != '.')
    lines += ['!macroend', '!macro UninstallPayload']
    lines.append('  ClearErrors')
    for rel in files:
        if str(rel) != 'dukebuilder.exe':
            target = str(rel).replace('\\', '/').replace('/', '\\').replace('$', '$$')
            lines.append(f'  Delete "$INSTDIR\\{target}"')
    lines += ['  ${If} ${Errors}',
              '    IfSilent +2',
              '    MessageBox MB_OK|MB_ICONSTOP "Some application files could not be removed. Close applications using this installation and try again."',
              '    SetErrorLevel 1', '    Quit', '  ${EndIf}']
    # Nonempty directories may contain user files and must be left intact.
    for rel in sorted(directories, key=lambda p: (-len(p.parts), str(p))):
        target = str(rel).replace('\\', '/').replace('/', '\\').replace('$', '$$')
        lines.append(f'  RMDir "$INSTDIR\\{target}"')
    lines += ['!macroend']
    if vc_redist is not None:
        lines.append(f'!define VC_REDIST {nsis_string(vc_redist)}')
    return '\n'.join(lines + [''])


def deploy_qt(stage, tool):
    """Deploy into a private copy; never modify the supplied Windows build."""
    compiler = shutil.which(tool)
    if not compiler:
        raise ValueError('windeployqt was not found. Pass --windeployqt PATH from the Windows build\'s Qt installation, or --skip-qt-deploy for an already deployed/static Qt payload.')
    subprocess.run([compiler, '--release', '--no-compiler-runtime',
                    '--dir', str(stage), str(stage / 'dukebuilder.exe')], check=True)
    # The platform plugin is loaded dynamically and cannot be inferred just by
    # looking for linked Qt DLLs. Fail instead of distributing a broken GUI.
    for required in ['Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll']:
        if not (stage / required).is_file():
            raise ValueError(f'Qt deployment did not produce {required}. Use the matching dynamic Qt 6 Windows installation.')



def check_redist(path):
    """Allow Microsoft's x86 bootstrapper for the x64 runtime package."""
    if path.name.lower() != 'vc_redist.x64.exe':
        raise ValueError('Supply the Microsoft x64 package named vc_redist.x64.exe.')
    with path.open('rb') as stream:
        header = stream.read(64)
        if len(header) != 64 or header[:2] != b'MZ':
            raise ValueError('The MSVC redistributable must be a Windows executable.')
        stream.seek(struct.unpack_from('<I', header, 60)[0])
        pe = stream.read(26)
        if len(pe) != 26 or pe[:4] != b'PE\0\0' or (struct.unpack_from('<H', pe, 4)[0], struct.unpack_from('<H', pe, 24)[0]) not in {(0x14c, 0x10b), (0x8664, 0x20b)}:
            raise ValueError('Invalid MSVC redistributable PE header.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path, help='Prepared deployment folder containing dukebuilder.exe and runtime dependencies')
    parser.add_argument('--output-dir', type=Path, default=REPO / 'dist')
    parser.add_argument('--version', help='Defaults to the project version in meson.build')
    parser.add_argument('--makensis', default='makensis', help='NSIS 3 compiler executable or path')
    parser.add_argument('--windeployqt', default='windeployqt', help='Matching Windows Qt deployment tool (default: find on PATH)')
    parser.add_argument('--skip-qt-deploy', action='store_true', help='Use an already deployed payload or a static Qt build')
    runtime = parser.add_mutually_exclusive_group(required=True)
    runtime.add_argument('--vc-redist', type=Path, help='Microsoft vc_redist.x64.exe to embed and install (at least as new as the build toolchain)')
    runtime.add_argument('--skip-vc-redist', action='store_true', help='Only for builds whose dependencies do not require the shared MSVC runtime')
    parser.add_argument('--check-only', action='store_true', help='Deploy and validate payload without compiling an installer')
    args = parser.parse_args()
    vc_redist = args.vc_redist.resolve() if args.vc_redist else None
    if vc_redist is not None:
        check_redist(vc_redist)
    stage = args.stage.resolve()
    files = payload_files(stage)
    version = args.version or re.search(r"version:\s*'([^']+)'", (REPO / 'meson.build').read_text())[1]
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:[-+][A-Za-z0-9.-]+)?', version):
        raise ValueError('Expected a version such as 0.1.0 or 0.1.0-beta.1.')
    output_dir = args.output_dir.resolve()
    if output_dir == stage or stage in output_dir.parents:
        raise ValueError('Output directory must be outside the staging directory.')
    config = make_config(stage, files, version, output_dir / 'unused.exe')
    compiler = None if args.check_only else shutil.which(args.makensis)
    if not args.check_only and not compiler:
        raise ValueError('NSIS 3 is required. Install makensis or pass --makensis PATH.')
    output_dir.mkdir(parents=True, exist_ok=True)
    name = f'DukeBuilder-{version}-x64-Setup.exe'
    with tempfile.TemporaryDirectory(prefix='.nsis-', dir=output_dir) as temporary:
        temp = Path(temporary)
        payload = temp / 'payload'
        shutil.copytree(stage, payload)
        if not args.skip_qt_deploy:
            deploy_qt(payload, args.windeployqt)
        # Include and validate the generated DLLs/plugins in both install and
        # uninstall manifests, and reject incorrect architectures after deploy.
        files = payload_files(payload)
        if args.check_only:
            print(f'Validated {len(files)} payload files for version {version}. Dependency completeness still requires Windows testing.')
            return
        if vc_redist is not None:
            bundled_redist = temp / 'vc_redist.x64.exe'
            shutil.copyfile(vc_redist, bundled_redist)
        else:
            bundled_redist = None
        config = make_config(payload, files, version, temp / name, bundled_redist)
        config_path = temp / 'payload.nsh'
        config_path.write_text(config, encoding='utf-8')
        prefix = '/' if os.name == 'nt' else '-'
        subprocess.run([compiler, prefix + 'V3', prefix + 'WX', prefix + 'NOCONFIG',
                        prefix + 'DCONFIG_FILE=' + str(config_path),
                        str(REPO / 'packaging/windows/dukebuilder.nsi')], check=True)
        os.replace(temp / name, output_dir / name)
    print(f'Created {output_dir / name}')


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
