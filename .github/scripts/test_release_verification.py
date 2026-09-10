"""Keep public downloads minimal while verifying their build-time checksums."""

import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(__file__).with_name("verify-release.sh").resolve()
spec = importlib.util.spec_from_file_location("release_artifacts", SCRIPT.with_name("release_artifacts.py"))
artifacts = importlib.util.module_from_spec(spec)
spec.loader.exec_module(artifacts)


class ReleaseVerificationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="release check ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.names = []
        for platform, arch in artifacts.TARGETS:
            extension = "dmg" if platform == "macos" else "zip"
            names = [f"K230BurningTool_{platform}_{arch}_normal_v1.0.0.{extension}"]
            if platform != "macos":
                extension = "run" if platform == "linux" else "exe"
                names.append(f"K230BurningToolIFW_{platform}_{arch}_normal_v1.0.0_setup.{extension}")
            entries = {}
            for name in names:
                path = self.root / name
                path.write_bytes(name.encode())
                entries[name] = artifacts.digest(path)
                self.names.append(name)
            artifacts.write_manifest(self.root / artifacts.manifest_name(platform, arch), entries)

    def verify(self, succeeds):
        result = subprocess.run(["bash", str(SCRIPT), str(self.root)], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, succeeds, result.stdout + result.stderr)

    def test_complete_release_generates_one_public_manifest(self):
        self.verify(True)
        self.assertEqual(len(artifacts.read_manifest(self.root / "SHA256SUMS")), 8)
        self.verify(True)

    def test_missing_package(self):
        (self.root / self.names[-1]).unlink()
        self.verify(False)

    def test_missing_installer(self):
        (self.root / self.names[1]).unlink()
        self.verify(False)

    def test_extra_artifacts_are_rejected(self):
        for name in (
            "K230BurningTool_windows_arm64_normal_v1.0.0.zip",
            "K230BurningTool_linux_x86_64_avalon_v1.0.0.zip",
            "K230BurningToolIFW_linux_x86_64_normal_v1.0.0_repository.tar.gz",
            "K230BurningTool_linux_normal_v1.0.0_x86_64.AppImage",
            self.names[0] + ".sha256",
            "unexpected.txt",
        ):
            with self.subTest(name=name):
                path = self.root / name
                path.write_text("unexpected")
                self.verify(False)
                path.unlink()

    def test_corrupt_archive(self):
        (self.root / self.names[0]).write_bytes(b"corrupted")
        self.verify(False)

    def test_missing_checksum_manifest(self):
        (self.root / artifacts.manifest_name("linux", "x86_64")).unlink()
        self.verify(False)

    def test_wrong_checksum_reference(self):
        path = self.root / artifacts.manifest_name("linux", "x86_64")
        path.write_bytes((self.root / artifacts.manifest_name("linux", "arm64")).read_bytes())
        self.verify(False)

    def test_duplicate_checksum_entry(self):
        path = self.root / artifacts.manifest_name("linux", "x86_64")
        path.write_text(path.read_text() + path.read_text().splitlines()[0] + "\n")
        self.verify(False)

    def test_duplicate_revision(self):
        (self.root / self.names[0].replace("v1.0.0", "v0.9.0")).write_bytes(b"older package")
        self.verify(False)

    def test_dirty_and_unsigned_names_are_rejected(self):
        original = self.names[0]
        manifest = self.root / artifacts.manifest_name("linux", "x86_64")
        entries = artifacts.read_manifest(manifest)
        for suffix in ("-dirty", "_unsigned"):
            with self.subTest(suffix=suffix):
                name = original.replace("v1.0.0", "v1.0.0" + suffix)
                (self.root / original).rename(self.root / name)
                updated = dict(entries)
                updated[name] = updated.pop(original)
                artifacts.write_manifest(manifest, updated)
                self.verify(False)
                (self.root / name).rename(self.root / original)
        artifacts.write_manifest(manifest, entries)

    def test_windows_crlf_checksum(self):
        path = self.root / artifacts.manifest_name("windows", "x86_64")
        path.write_bytes(path.read_bytes().replace(b"\n", b"\r\n"))
        self.verify(True)


class ArtifactCollectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="collect artifacts ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / "build"
        self.output = self.root / "public"
        (self.build / "ifw-artifacts/repositories").mkdir(parents=True)
        self.portable = self.add(self.build / "K230BurningTool_linux_x86_64_normal_v1.0.0.zip")
        self.installer = self.add(self.build / "ifw-artifacts/K230BurningToolIFW_linux_x86_64_normal_v1.0.0_setup.run")
        self.add(self.build / "ifw-artifacts/repositories/K230BurningToolIFW_linux_x86_64_normal_v1.0.0_repository.tar.gz")

    def add(self, path):
        path.write_bytes(path.name.encode())
        artifacts.write_manifest(path.with_name(path.name + ".sha256"), {path.name: artifacts.digest(path)})
        return path

    def test_collects_only_packages_and_one_manifest(self):
        artifacts.collect(self.build, self.output, "linux", "x86_64")
        self.assertEqual({path.name for path in self.output.iterdir()},
                         {self.portable.name, self.installer.name, "SHA256SUMS_linux_x86_64.txt"})

    def test_checks_original_hash_before_copying(self):
        self.installer.write_bytes(b"corrupted")
        with self.assertRaisesRegex(ValueError, "Checksum does not match"):
            artifacts.collect(self.build, self.output, "linux", "x86_64")
        self.assertFalse(self.output.exists())

    def test_unsigned_macos_is_explicitly_opt_in(self):
        package = self.add(self.build / "K230BurningTool_macos_arm64_normal_abcd_unsigned.dmg")
        with self.assertRaises(ValueError):
            artifacts.collect(self.build, self.output, "macos", "arm64")
        artifacts.collect(self.build, self.output, "macos", "arm64", allow_unsigned=True)
        self.assertTrue((self.output / package.name).is_file())


if __name__ == "__main__":
    unittest.main()
