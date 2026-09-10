"""Regression checks for the macOS signing and distribution path."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class MacOSDistributionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.workflow = (ROOT / ".github/workflows/build.yml").read_text()
        cls.packager = (ROOT / "gui/mac-install.cmake").read_text()
        cls.logger = (ROOT / "gui/src/widgets/LoggerWindow.cpp").read_text()

    def test_branch_and_tag_builds_use_the_signing_job(self):
        build_job = self.workflow.split("\n  build-macos:\n", 1)[1].split(
            "\n  sign-macos:\n", 1
        )[0]
        sign_job = self.workflow.split("\n  sign-macos:\n", 1)[1].split(
            "\n  release:\n", 1
        )[0]

        self.assertIn('MACOS_BUILD_ONLY: "1"', build_job)
        self.assertNotIn("startsWith(github.ref, 'refs/tags/')", build_job)
        self.assertNotIn("--allow-unsigned", build_job)
        self.assertNotIn("validation DMG", build_job)
        self.assertNotIn("startsWith(github.ref, 'refs/tags/')", sign_job)
        self.assertIn("environment: macos-signing", sign_job)

    def test_packager_repairs_and_verifies_local_app_signatures(self):
        self.assertIn("--deep --force --sign -", self.packager)
        self.assertIn("--verify --deep --strict --verbose=2", self.packager)

    def test_notarized_outputs_are_assessed_by_gatekeeper(self):
        self.assertIn("--assess --type execute --verbose=4", self.packager)
        self.assertIn("--assess --type open", self.packager)
        self.assertIn("--context context:primary-signature", self.packager)

    def test_macos_log_is_outside_the_signed_app_bundle(self):
        self.assertIn("#ifdef Q_OS_MACOS", self.logger)
        self.assertIn("QStandardPaths::AppLocalDataLocation", self.logger)


if __name__ == "__main__":
    unittest.main()
