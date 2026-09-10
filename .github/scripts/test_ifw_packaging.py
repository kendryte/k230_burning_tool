"""Validate IFW package metadata without downloading tools or changing a desktop."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ifw_build", ROOT / "packaging/ifw/package.py")
ifw = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ifw)


class IfwPackagingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="ifw packaging ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / "deployed"
        (self.source / "bin").mkdir(parents=True)
        for name in ("K230BurningTool", "K230BurningTool.exe", "Avalon_Home_Series_Firmware_Upgrade_Tool.exe"):
            (self.source / "bin" / name).write_bytes(b"application fixture")
        (self.source / "lib").mkdir()
        (self.source / "lib/runtime-library").write_bytes(b"runtime fixture")

    def prepare(self, platform="linux", arch="x86_64", variant="normal", version="2.2.4", base=""):
        work = self.root / (platform + arch + variant)
        info = ifw.prepare(self.source, work, platform, arch, variant, version, base)
        return work, info

    def test_supported_variants_and_architectures(self):
        for platform, arch in sorted(ifw.SUPPORTED):
            for variant in ("normal", "avalon"):
                with self.subTest(platform=platform, arch=arch, variant=variant):
                    work, info = self.prepare(platform, arch, variant, base="https://updates.example.test/k230")
                    component = next((work / "packages").iterdir())
                    config = ET.parse(work / "config/config.xml")
                    package = ET.parse(component / "meta/package.xml")
                    self.assertEqual(package.findtext("Version"), "2.2.4")
                    self.assertEqual(info["repository"], f"https://updates.example.test/k230/{platform}/{arch}/{variant}")
                    self.assertEqual(config.findtext("RemoteRepositories/Repository/Url"), info["repository"])
                    marker = json.loads((component / "data/ifw-installation.json").read_text())
                    self.assertEqual(marker, info)
                    self.assertTrue((component / "data" / info["executable"]).is_file())
                    self.assertEqual((component / "data/app/lib/runtime-library").read_bytes(), b"runtime fixture")
                    icon = "icon_avalon.png" if variant == "avalon" else "icons/icon_256x256.png"
                    self.assertEqual((component / "data/app-icon.png").read_bytes(),
                                     (ROOT / "gui/resources" / icon).read_bytes())
                    self.assertIn("addStopProcessForUpdateRequest", (component / "meta/installscript.qs").read_text())
        self.assertFalse((self.source / "ifw-installation.json").exists())

    def test_offline_config_has_no_invented_repository(self):
        work, info = self.prepare()
        self.assertEqual(info["repository"], "")
        self.assertIsNone(ET.parse(work / "config/config.xml").find("RemoteRepositories"))

    def test_bad_repository_urls(self):
        for value in ("http://example.com", "file:///tmp/repo", "https://user:pass@example.com",
                      "https://example.com?token=secret", "https://example.com/#fragment", "https://"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                ifw.repository_url(value, "linux", "arm64", "normal")

    def test_xml_escaping(self):
        work, _ = self.prepare(base="https://example.test/a&b")
        self.assertEqual(ET.parse(work / "config/config.xml").findtext("RemoteRepositories/Repository/Url"),
                         "https://example.test/a&b/linux/x86_64/normal")

    def test_invalid_version_and_platform(self):
        for version in ("v2.2.4", "2.2.4-beta", "2.2", "../2.2.4"):
            with self.subTest(version=version), self.assertRaises(ValueError):
                self.prepare(version=version)
        with self.assertRaises(ValueError):
            self.prepare("windows", "arm64")

    def test_missing_executable(self):
        (self.source / "bin/K230BurningTool").unlink()
        with self.assertRaises(ValueError):
            self.prepare()

    def test_nested_output_is_rejected(self):
        with self.assertRaises(ValueError):
            ifw.prepare(self.source, self.source / "output", "linux", "x86_64", "normal", "2.2.4")

    def test_checksum(self):
        artifact = self.root / "test.zip"
        artifact.write_bytes(b"abc")
        ifw.checksum(artifact)
        self.assertNotIn(b"\r", artifact.with_name("test.zip.sha256").read_bytes())
        self.assertEqual(artifact.with_name("test.zip.sha256").read_text(),
                         "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  test.zip\n")


if __name__ == "__main__":
    unittest.main()
