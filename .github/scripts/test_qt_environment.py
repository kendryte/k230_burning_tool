"""Test Qt cache detection and environment export without installing Qt."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).with_name("qt-environment.sh")


class QtEnvironmentTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="qt cache test ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.qt = self.root / "qt-6.6.3/Qt/6.6.3/macos"
        self.env = dict(os.environ, RUNNER_TOOL_CACHE=str(self.root), QT_VERSION="6.6.3")
        for name in ("GITHUB_OUTPUT", "GITHUB_ENV", "GITHUB_PATH"):
            path = self.root / name
            path.touch()
            self.env[name] = str(path)
        self.env.pop("PKG_CONFIG_PATH", None)

    def install_fixture(self, version="6.6.3"):
        for tool in ("qmake", "qt-cmake", "macdeployqt", "lrelease", "lupdate"):
            path = self.qt / "bin" / tool
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(f"#!/bin/bash\nprintf '%s\\n' '{version}'\n")
            path.chmod(0o755)
        for module in ("Qt6", "Qt6Core", "Qt6Widgets", "Qt6LinguistTools", "Qt6Network", "Qt6Svg"):
            path = self.qt / "lib/cmake" / module / f"{module}Config.cmake"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        plugin = self.qt / "plugins/platforms/libqcocoa.dylib"
        plugin.parent.mkdir(parents=True)
        plugin.touch()

    def invoke(self, mode):
        return subprocess.run(["bash", str(SCRIPT), mode], env=self.env, capture_output=True, text=True)

    def assert_available(self, expected):
        Path(self.env["GITHUB_OUTPUT"]).write_text("")
        result = self.invoke("check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(Path(self.env["GITHUB_OUTPUT"]).read_text(), f"available={expected}\n")

    def test_missing_installation(self):
        self.assert_available("false")

    def test_valid_installation(self):
        self.install_fixture()
        self.assert_available("true")

    def test_wrong_version(self):
        self.install_fixture(version="6.7.0")
        self.assert_available("false")

    def test_incomplete_installation(self):
        self.install_fixture()
        for name in ("bin/macdeployqt", "lib/cmake/Qt6Svg/Qt6SvgConfig.cmake",
                     "plugins/platforms/libqcocoa.dylib"):
            with self.subTest(name=name):
                path = self.qt / name
                data, mode = path.read_bytes(), path.stat().st_mode
                path.unlink()
                self.assert_available("false")
                path.write_bytes(data)
                path.chmod(mode)

    def test_activate_exports_paths_with_spaces(self):
        self.install_fixture()
        self.env["PKG_CONFIG_PATH"] = "/existing/lib/pkgconfig"
        result = self.invoke("activate")
        self.assertEqual(result.returncode, 0, result.stderr)
        exported = dict(line.split("=", 1) for line in Path(self.env["GITHUB_ENV"]).read_text().splitlines())
        self.assertEqual(exported["QT_ROOT_DIR"], str(self.qt))
        self.assertEqual(exported["QT_CMAKE"], str(self.qt / "bin/qt-cmake"))
        self.assertEqual(exported["QT_PLUGIN_PATH"], str(self.qt / "plugins"))
        self.assertEqual(exported["QML2_IMPORT_PATH"], str(self.qt / "qml"))
        self.assertEqual(exported["PKG_CONFIG_PATH"], f"{self.qt}/lib/pkgconfig:/existing/lib/pkgconfig")
        self.assertEqual(Path(self.env["GITHUB_PATH"]).read_text(), f"{self.qt}/bin\n")

    def test_invalid_installation_cannot_be_activated(self):
        result = self.invoke("activate")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(Path(self.env["GITHUB_ENV"]).read_text(), "")

    def test_interrupted_installation_is_not_reused(self):
        self.install_fixture()
        self.assertEqual(self.invoke("prepare-install").returncode, 0)
        self.assert_available("false")
        self.assertEqual(self.invoke("activate").returncode, 0)
        self.assert_available("true")

    def test_failed_activation_keeps_pending_marker(self):
        self.assertEqual(self.invoke("prepare-install").returncode, 0)
        self.assertNotEqual(self.invoke("activate").returncode, 0)
        self.assertTrue((self.root / "qt-6.6.3/.installation-pending").exists())


if __name__ == "__main__":
    unittest.main()
