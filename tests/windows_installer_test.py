"""Packaging checks; synthetic PE headers are fixtures, never release binaries."""
import importlib.util
import os
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('installer', Path(__file__).resolve().parents[1] / 'scripts/build-windows-installer.py')
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
            config = installer.make_config(stage, files, '0.1.0', stage / 'setup.exe')
            self.assertIn('Delete "$INSTDIR\\platforms\\qwindows.dll"', config)
            self.assertIn('RMDir "$INSTDIR\\platforms"', config)
            self.assertNotIn('RMDir /r', config)
            self.assertNotIn('Delete "$INSTDIR\\*', config)
            (stage / 'personal.map').write_bytes(b'map')
            with self.assertRaises(ValueError):
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

    def test_escaping(self):
        self.assertEqual(installer.nsis_string('a$b"c'), '"a$$b$\\"c"')
        with self.assertRaises(ValueError):
            installer.nsis_string('a\nb')


if __name__ == '__main__':
    unittest.main()
