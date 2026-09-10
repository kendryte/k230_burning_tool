"""Opt-in real IFW install/update/uninstall smoke test, isolated in a temporary HOME."""

import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]


def find_installer(output, host):
    extension = {"linux": "run", "windows": "exe"}[host]
    candidates = [path for path in output.glob("*_setup." + extension) if path.is_file()]
    if len(candidates) != 1:
        raise ValueError("Expected exactly one ." + extension + " installer in " + str(output))
    return candidates[0]


class InstallerSelectionTests(unittest.TestCase):
    def test_selects_platform_binary_not_checksum(self):
        for host, extension in (("linux", "run"), ("windows", "exe")):
            with self.subTest(host=host), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary)
                installer = output / ("package_setup." + extension)
                installer.with_name(installer.name + ".sha256").write_text("checksum")
                installer.write_bytes(b"installer fixture")
                other = "exe" if extension == "run" else "run"
                (output / ("package_setup." + other)).write_bytes(b"other platform")
                self.assertEqual(find_installer(output, host), installer)

    def test_checksum_without_binary_is_rejected(self):
        for host, extension in (("linux", "run"), ("windows", "exe")):
            with self.subTest(host=host), tempfile.TemporaryDirectory() as temporary:
                output = Path(temporary)
                (output / ("package_setup." + extension + ".sha256")).write_text("checksum")
                with self.assertRaisesRegex(ValueError, "Expected exactly one"):
                    find_installer(output, host)

    def test_multiple_installers_are_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            (output / "first_setup.run").touch()
            (output / "second_setup.run").touch()
            with self.assertRaisesRegex(ValueError, "Expected exactly one"):
                find_installer(output, "linux")

    def test_matching_directory_is_not_an_installer(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary)
            (output / "package_setup.run").mkdir()
            with self.assertRaisesRegex(ValueError, "Expected exactly one"):
                find_installer(output, "linux")


@unittest.skipUnless(os.environ.get("K230_TEST_IFW_TOOLS"), "Set K230_TEST_IFW_TOOLS for native IFW tests")
class IfwInstallerTests(unittest.TestCase):
    def test_offline_build_does_not_generate_repository_bundles(self):
        with tempfile.TemporaryDirectory(prefix="ifw offline ") as temporary:
            root = Path(temporary)
            host = "windows" if os.name == "nt" else "linux"
            arch = "arm64" if platform.machine().lower() in ("aarch64", "arm64") else "x86_64"
            source = root / "source"
            (source / "bin").mkdir(parents=True)
            executable = source / "bin" / ("K230BurningTool.exe" if host == "windows" else "K230BurningTool")
            executable.write_bytes(b"fixture")
            executable.chmod(0o755)
            output = root / "output"
            subprocess.run([sys.executable, str(ROOT / "packaging/ifw/package.py"),
                            "--source", str(source), "--output", str(output),
                            "--tools", os.environ["K230_TEST_IFW_TOOLS"],
                            "--platform", host, "--arch", arch, "--variant", "normal",
                            "--version", "1.0.0", "--revision", "1.0.0"],
                           check=True, env=dict(os.environ, QT_QPA_PLATFORM="offscreen"),
                           capture_output=True, timeout=180)
            installer = find_installer(output, host)
            self.assertEqual({path.name for path in output.iterdir()}, {installer.name, installer.name + ".sha256"})

    def test_install_update_and_uninstall(self):
        with tempfile.TemporaryDirectory(prefix="ifw smoke ") as temporary:
            root = Path(temporary)
            host = "windows" if os.name == "nt" else "linux"
            arch = "arm64" if platform.machine().lower() in ("aarch64", "arm64") else "x86_64"
            ext = ".exe" if host == "windows" else ""
            env = dict(os.environ, QT_QPA_PLATFORM="offscreen", HOME=str(root / "home"),
                       XDG_DATA_HOME=str(root / "data"), XDG_CONFIG_HOME=str(root / "config"))
            # IFW shortcuts must stay inside the test, including Windows known folders.
            source = root / "source"
            (source / "bin").mkdir(parents=True)
            executable = source / "bin" / ("K230BurningTool" + ext)
            installed = root / "installed"
            repositories = []
            shortcuts = host == "linux" and os.geteuid() != 0
            shortcut_option = "CreateShortcuts=" + ("true" if shortcuts else "false")
            contents = {}
            for version in ("1.0.0", "1.0.1"):
                contents[version] = ("#!/bin/sh\nprintf '%s' '" + version + "' > \"$K230_TEST_APP_OUTPUT\"\n").encode() if host == "linux" else version.encode()
                executable.write_bytes(contents[version])
                executable.chmod(0o755)
                output = root / version
                command = [sys.executable, str(ROOT / "packaging/ifw/package.py"),
                           "--source", str(source), "--output", str(output),
                           "--tools", os.environ["K230_TEST_IFW_TOOLS"],
                           "--platform", host, "--arch", arch, "--variant", "normal",
                           "--version", version, "--revision", version,
                           "--repository-base", "https://updates.example.invalid/k230"]
                subprocess.run(command, check=True, env=env, capture_output=True, timeout=180)
                repository = root / ("repository-" + version)
                repository.mkdir()
                with tarfile.open(next((output / "repositories").glob("*_repository.tar.gz"))) as archive:
                    if hasattr(tarfile, "data_filter"):
                        archive.extractall(repository, filter="data")
                    else:
                        # The archive was just generated from this test's own fixture.
                        archive.extractall(repository)
                repositories.append(repository)
                if version == "1.0.0":
                    installer = find_installer(output, host)
                    installer.chmod(0o755)
                    result = subprocess.run([str(installer), "--root", str(installed),
                                             "--accept-licenses", "--default-answer", "--confirm-command",
                                             "install", shortcut_option],
                                            env=env, capture_output=True, text=True, timeout=90)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertEqual((installed / "app/bin" / executable.name).read_bytes(), contents["1.0.0"])
                    if shortcuts:
                        self.assertTrue((root / "data/applications/K230BurningTool-Installed.desktop").is_file())
            tool = installed / ("MaintenanceTool" + ext)
            self.assertTrue(tool.is_file())
            result = subprocess.run([str(tool), "--set-temp-repository", repositories[1].as_uri(),
                                     "--accept-licenses", "--default-answer", "--confirm-command", "update", shortcut_option],
                                    env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((installed / "app/bin" / executable.name).read_bytes(), contents["1.0.1"])
            self.assertEqual(json.loads((installed / "ifw-installation.json").read_text())["version"], "1.0.1")
            if shortcuts and shutil.which("gio"):
                desktop = root / "data/applications/K230BurningTool-Installed.desktop"
                if shutil.which("desktop-file-validate"):
                    subprocess.run(["desktop-file-validate", str(desktop)], check=True, capture_output=True)
                env["K230_TEST_APP_OUTPUT"] = str(root / "launched-version")
                subprocess.run(["gio", "launch", str(desktop)], check=True, env=env, capture_output=True)
                for _ in range(100):
                    if Path(env["K230_TEST_APP_OUTPUT"]).exists():
                        break
                    time.sleep(0.05)
                self.assertEqual(Path(env["K230_TEST_APP_OUTPUT"]).read_text(), "1.0.1")
            result = subprocess.run([str(tool), "--default-answer", "--confirm-command", "purge"],
                                    env=env, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertFalse((installed / "app/bin" / executable.name).exists())
            self.assertFalse((installed / "ifw-installation.json").exists())
            # Windows uses a short-lived helper to remove the running maintenance executable.
            for _ in range(200):
                if not tool.exists():
                    break
                time.sleep(0.05)
            self.assertFalse(tool.exists())
            if shortcuts:
                self.assertFalse((root / "data/applications/K230BurningTool-Installed.desktop").exists())


if __name__ == "__main__":
    unittest.main()
