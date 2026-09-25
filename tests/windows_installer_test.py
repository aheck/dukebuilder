"""Packaging checks; synthetic PE headers are fixtures, never release binaries."""
import importlib.util
import os
from pathlib import Path
import re
import struct
import tempfile
import unittest
from unittest import mock
import subprocess

ROOT = Path(__file__).resolve().parents[1]
meson_build = (ROOT / 'meson.build').read_text(encoding='utf-8')
version_match = re.search(r"(?m)^\s*version:\s*'([^']+)'", meson_build)
if version_match is None:
    raise RuntimeError('Could not find project version in meson.build')
VERSION = version_match.group(1)

spec = importlib.util.spec_from_file_location('installer', ROOT / 'scripts/build-windows-installer.py')
installer = importlib.util.module_from_spec(spec)
spec.loader.exec_module(installer)


def write_pe(path, machine=0x8664):
    data = bytearray(128)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 64)
    data[64:68] = b'PE\0\0'
    struct.pack_into('<H', data, 68, machine)
    struct.pack_into('<H', data, 88, 0x20b)
    path.write_bytes(data)


class InstallerTests(unittest.TestCase):
    def test_non_windows_and_wrong_architecture(self):
        with tempfile.TemporaryDirectory() as temp:
            exe = Path(temp) / 'dukebuilder.exe'
            exe.write_bytes(b'\x7fELF' + bytes(124))
            with self.assertRaises(ValueError):
                installer.payload_files(Path(temp))
            write_pe(exe, 0x14c)
            with self.assertRaises(ValueError):
                installer.payload_files(Path(temp))

    def test_manifest_preserves_unowned_files(self):
        with tempfile.TemporaryDirectory(prefix='installer space ') as temp:
            stage = Path(temp)
            write_pe(stage / 'dukebuilder.exe')
            (stage / 'platforms').mkdir()
            write_pe(stage / 'platforms/qwindows.dll')
            (stage / 'licenses').mkdir()
            (stage / 'licenses/notice.txt').write_text('notice')
            files = installer.payload_files(stage)
            config = installer.make_config(stage, files, VERSION, stage / 'setup.exe')
            self.assertIn('Delete "$INSTDIR\\platforms\\qwindows.dll"', config)
            self.assertIn('RMDir "$INSTDIR\\platforms"', config)
            self.assertNotIn('RMDir /r', config)
            self.assertNotIn('Delete "$INSTDIR\\*', config)
            (stage / 'personal.map').write_bytes(b'map')
            with self.assertRaises(ValueError):
                installer.payload_files(stage)

    def test_scope_metadata_is_reserved(self):
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp)
            write_pe(stage / 'dukebuilder.exe')
            (stage / 'INSTALL-SCOPE.INI').write_text('[Installation]\nScope=AllUsers\n')
            with self.assertRaisesRegex(ValueError, 'reserved files'):
                installer.payload_files(stage)

    def test_case_collision(self):
        if os.name == 'nt':
            self.skipTest('Case-sensitive staging fixture requires POSIX')
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp)
            write_pe(stage / 'dukebuilder.exe')
            write_pe(stage / 'DUKEBUILDER.EXE')
            with self.assertRaises(ValueError):
                installer.payload_files(stage)

    def test_automatic_deployment_manifest_and_source_preservation(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stage = root / 'source with spaces'
            stage.mkdir()
            write_pe(stage / 'dukebuilder.exe')
            output = root / 'output'
            calls = []
            write_pe(root / 'vc_redist.x64.exe')

            def run(command, check):
                calls.append(command)
                if command[0] == 'qt-tool':
                    payload = Path(command[command.index('--dir') + 1])
                    self.assertNotEqual(payload, stage)
                    self.assertIn('--release', command)
                    for name in ['Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll']:
                        target = payload / name
                        target.parent.mkdir(parents=True, exist_ok=True)
                        write_pe(target)
                else:
                    config_path = Path(next(arg.split('=', 1)[1] for arg in command if 'DCONFIG_FILE=' in arg))
                    config = config_path.read_text()
                    self.assertIn('Delete "$INSTDIR\\platforms\\qwindows.dll"', config)
                    self.assertIn('Qt6Core.dll', config)
                    self.assertIn('!define VC_REDIST', config)
                    self.assertEqual((config_path.parent / 'vc_redist.x64.exe').read_bytes(), (root / 'vc_redist.x64.exe').read_bytes())
                    (config_path.parent / f'DukeBuilder-{VERSION}-x64-Setup.exe').write_bytes(b'test installer')

            argv = ['packager', str(stage), '--version', VERSION, '--output-dir', str(output), '--windeployqt', 'qt-tool', '--vc-redist', str(root / 'vc_redist.x64.exe')]
            with mock.patch.object(installer.sys, 'argv', argv), mock.patch.object(installer.shutil, 'which', side_effect=lambda tool: tool), mock.patch.object(installer.subprocess, 'run', side_effect=run):
                installer.main()
            self.assertEqual(len(calls), 2)
            self.assertEqual(list(stage.iterdir()), [stage / 'dukebuilder.exe'])
            self.assertTrue((output / f'DukeBuilder-{VERSION}-x64-Setup.exe').is_file())

    def test_deployment_failure_and_missing_plugin(self):
        with tempfile.TemporaryDirectory() as temp:
            stage = Path(temp)
            write_pe(stage / 'dukebuilder.exe')
            with mock.patch.object(installer.shutil, 'which', return_value=None):
                with self.assertRaisesRegex(ValueError, 'windeployqt was not found'):
                    installer.deploy_qt(stage, 'missing')
            with mock.patch.object(installer.shutil, 'which', return_value='qt-tool'), mock.patch.object(installer.subprocess, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'Qt6Core.dll'):
                    installer.deploy_qt(stage, 'qt-tool')
                run.side_effect = subprocess.CalledProcessError(1, 'qt-tool')
                with self.assertRaises(subprocess.CalledProcessError):
                    installer.deploy_qt(stage, 'qt-tool')

    def test_redistributable_bootstrapper_validation(self):
        with tempfile.TemporaryDirectory() as temp:
            redist = Path(temp) / 'vc_redist.x64.exe'
            write_pe(redist)
            installer.check_redist(redist)
            # The x64 runtime installer may itself be a 32-bit executable.
            data = bytearray(redist.read_bytes())
            struct.pack_into('<H', data, 68, 0x14c)
            struct.pack_into('<H', data, 88, 0x10b)
            redist.write_bytes(data)
            installer.check_redist(redist)
            redist.write_bytes(b'not an executable')
            with self.assertRaises(ValueError):
                installer.check_redist(redist)
            with self.assertRaises(ValueError):
                installer.check_redist(Path(temp) / 'vc_redist.x86.exe')

    def test_explicit_runtime_opt_out(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            stage = root / 'stage'
            stage.mkdir()
            write_pe(stage / 'dukebuilder.exe')
            argv = ['packager', str(stage), '--output-dir', str(root / 'out'),
                    '--skip-qt-deploy', '--skip-vc-redist', '--check-only']
            with mock.patch.object(installer.sys, 'argv', argv):
                installer.main()
            config = installer.make_config(stage, installer.payload_files(stage), VERSION, root / 'setup.exe')
            self.assertNotIn('!define VC_REDIST', config)

    def test_escaping(self):
        self.assertEqual(installer.nsis_string('a$b"c'), '"a$$b$\\"c"')
        with self.assertRaises(ValueError):
            installer.nsis_string('a\nb')


if __name__ == '__main__':
    unittest.main()
