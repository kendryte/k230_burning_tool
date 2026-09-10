"""Require both architectures and variants, including each Linux AppImage."""

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
        for platform, extension in (("linux", "tar.gz"), ("windows", "zip"), ("macos", "dmg")):
            for arch in ("x86_64", "arm64"):
                for variant in ("normal", "avalon"):
                    self.add_package(f"K230BurningTool_{platform}_{arch}_{variant}_v1.0.0.{extension}")
                    if platform == "linux":
                        self.add_package(f"K230BurningTool_linux_{variant}_v1.0.0_{arch}.AppImage")

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

    def test_missing_appimage(self):
        (self.root / self.names[1]).unlink()
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
        name = next(name for name in self.names if name.endswith(".zip"))
        path = self.root / (name + ".sha256")
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.verify(True)


if __name__ == "__main__":
    unittest.main()
