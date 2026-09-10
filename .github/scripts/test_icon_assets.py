"""Check icon resource paths and the optional Pillow-based Windows converter."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

try:
    from PIL import Image
except ImportError:
    Image = None

ROOT = Path(__file__).resolve().parents[2]
RESOURCES = ROOT / "gui/resources"
SCRIPT = Path(__file__).with_name("generate_windows_icon.py")


class IconResourceTests(unittest.TestCase):
    def test_qt_resources_exist(self):
        entries = ET.parse(RESOURCES / "main.qrc").findall(".//file")
        for entry in entries:
            with self.subTest(path=entry.text):
                self.assertTrue((RESOURCES / entry.text).is_file())
        paths = {entry.text for entry in entries}
        for size in (16, 32, 128, 256, 512):
            self.assertIn(f"icons/icon_{size}x{size}.png", paths)
            self.assertIn(f"icons/icon_{size}x{size}@2x.png", paths)


@unittest.skipUnless(Image, "Pillow is required only for icon conversion tests")
class WindowsIconTests(unittest.TestCase):
    def setUp(self):
        spec = importlib.util.spec_from_file_location("generate_windows_icon", SCRIPT)
        self.converter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.converter)
        self.temp = tempfile.TemporaryDirectory(prefix="k230 icon ")
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name) / "icon.ico"

    def check_icon(self, path):
        with Image.open(path) as icon:
            self.assertEqual(icon.format, "ICO")
            self.assertEqual(icon.ico.sizes(), {(size, size) for size in self.converter.SIZES})
            for size, filename in self.converter.SOURCES.items():
                with self.subTest(size=size), Image.open(RESOURCES / "icons" / filename) as source:
                    self.assertEqual(icon.ico.getimage((size, size)).convert("RGBA").tobytes(),
                                     source.convert("RGBA").tobytes())

    def test_conversion_preserves_supplied_pixels_and_alpha(self):
        self.converter.generate(RESOURCES / "icons", self.output)
        self.check_icon(self.output)

    def test_resource_ico_matches_current_pngs(self):
        self.check_icon(RESOURCES / "icons/icon.ico")

    def test_cli_works_outside_repository(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--output", str(self.output)],
                                cwd=self.temp.name, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.check_icon(self.output)

    def test_wrong_size_is_rejected_without_overwriting_output(self):
        Image.new("RGBA", (17, 17)).save(Path(self.temp.name) / "icon_16x16.png")
        self.output.write_bytes(b"existing icon")
        with self.assertRaisesRegex(ValueError, "must be 16x16"):
            self.converter.generate(Path(self.temp.name), self.output)
        self.assertEqual(self.output.read_bytes(), b"existing icon")


if __name__ == "__main__":
    unittest.main()
