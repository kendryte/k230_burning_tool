"""Require portable packages, IFW installers/repositories, and signed macOS DMGs."""

import hashlib
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("verify-release.sh")


class ReleaseVerificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="release check ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.names = []
        for platform, extension in (("linux", "zip"), ("windows", "zip"), ("macos", "dmg")):
            for arch in ("x86_64", "arm64"):
                if platform == "windows" and arch == "arm64":
                    continue
                for variant in ("normal",):
                    self.add_package(f"K230BurningTool_{platform}_{arch}_{variant}_v1.0.0.{extension}")
                    if platform != "macos":
                        setup = "run" if platform == "linux" else "exe"
                        self.add_package(f"K230BurningToolIFW_{platform}_{arch}_{variant}_v1.0.0_setup.{setup}")
                        self.add_package(f"K230BurningToolIFW_{platform}_{arch}_{variant}_v1.0.0_repository.tar.gz")

    def add_package(self, name):
        self.names.append(name)
        data = name.encode()
        (self.root / name).write_bytes(data)
        (self.root / (name + ".sha256")).write_text(f"{hashlib.sha256(data).hexdigest()}  {name}\n")

    def verify(self, succeeds):
        result = subprocess.run(["bash", str(SCRIPT), str(self.root)], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)

    def test_complete_release(self):
        self.verify(True)

    def test_missing_variant(self):
        (self.root / self.names[-1]).unlink()
        self.verify(False)

    def test_missing_windows_x86_64_variant(self):
        name = next(name for name in self.names if name.startswith("K230BurningTool_windows_") and name.endswith(".zip"))
        (self.root / name).unlink()
        self.verify(False)

    def test_unexpected_windows_arm64_package(self):
        self.add_package("K230BurningTool_windows_arm64_normal_v1.0.0.zip")
        self.verify(False)

    def test_unexpected_avalon_artifacts(self):
        for name in (
            "K230BurningTool_linux_x86_64_avalon_v1.0.0.zip",
            "K230BurningTool_windows_x86_64_avalon_v1.0.0.zip",
            "K230BurningTool_macos_arm64_avalon_v1.0.0.dmg",
            "K230BurningToolIFW_linux_x86_64_avalon_v1.0.0_setup.run",
            "K230BurningToolIFW_linux_x86_64_avalon_v1.0.0_repository.tar.gz",
        ):
            with self.subTest(name=name):
                self.add_package(name)
                self.verify(False)
                (self.root / name).unlink()
                (self.root / (name + ".sha256")).unlink()

    def test_missing_installer(self):
        (self.root / self.names[1]).unlink()
        self.verify(False)

    def test_missing_repository(self):
        (self.root / self.names[2]).unlink()
        self.verify(False)

    def test_obsolete_appimage_is_rejected(self):
        self.add_package("K230BurningTool_linux_normal_v1.0.0_x86_64.AppImage")
        self.verify(False)

    def test_unknown_file_is_rejected(self):
        (self.root / "unexpected.txt").write_text("unexpected")
        self.verify(False)

    def test_corrupt_archive(self):
        (self.root / self.names[0]).write_bytes(b"corrupted")
        self.verify(False)

    def test_missing_checksum(self):
        (self.root / (self.names[0] + ".sha256")).unlink()
        self.verify(False)

    def test_wrong_checksum_reference(self):
        (self.root / (self.names[0] + ".sha256")).write_text(
            (self.root / (self.names[1] + ".sha256")).read_text()
        )
        self.verify(False)

    def test_duplicate_revision(self):
        self.add_package(self.names[0].replace("v1.0.0", "v0.9.0"))
        self.verify(False)

    def test_unsigned_input(self):
        self.add_package("unsigned.dmg")
        self.verify(False)

    def test_windows_crlf_checksum(self):
        name = next(name for name in self.names if name.startswith("K230BurningTool_windows_") and name.endswith(".zip"))
        path = self.root / (name + ".sha256")
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.verify(True)


if __name__ == "__main__":
    unittest.main()
