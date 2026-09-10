"""Exercise the packaged runtime without FUSE, flags, or extraction env vars.

Set K230_TEST_APPIMAGE to a packaged image. Run in a container without /dev/fuse,
or set K230_TEST_DOCKER_IMAGE to a local container image containing /bin/sh.
The host running these tests needs mksquashfs.
"""

import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPACK = Path(__file__).resolve().parents[2] / "gui" / "repack-appimage.sh"


class RepackValidationTests(unittest.TestCase):
    def test_invalid_architecture_leaves_image_unchanged(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "invalid.AppImage"
            data = bytes(64)
            image.write_bytes(data)
            for arch, message in (("riscv64", "Unsupported"), ("x86_64", "does not match")):
                with self.subTest(arch=arch):
                    result = subprocess.run(
                        ["bash", str(REPACK), str(image), arch],
                        capture_output=True, text=True, timeout=10,
                    )
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(message, result.stderr)
                    self.assertEqual(image.read_bytes(), data)


@unittest.skipUnless(os.environ.get("K230_TEST_APPIMAGE"), "Set K230_TEST_APPIMAGE")
class AppImageRuntimeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        image = Path(os.environ["K230_TEST_APPIMAGE"]).resolve()
        offset = int(subprocess.check_output([str(image), "--appimage-offset"]))
        with image.open("rb") as source:
            cls.runtime = source.read(offset)
        if b"URUNTIME_EXTRACT=2" not in cls.runtime:
            raise AssertionError("The packaged runtime must enable automatic extraction")
        cls.docker_image = os.environ.get("K230_TEST_DOCKER_IMAGE")
        if not cls.docker_image and Path("/dev/fuse").exists():
            raise AssertionError("Run without /dev/fuse or set K230_TEST_DOCKER_IMAGE")

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="appimage runtime ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        appdir = self.root / "AppDir"
        appdir.mkdir()
        apprun = appdir / "AppRun"
        apprun.write_text(
            '#!/bin/sh\n'
            'printf "%s\\n" "$APPIMAGE" "$PWD" "$@"\n'
            'exit "${PROBE_EXIT_CODE:-0}"\n'
        )
        apprun.chmod(0o755)
        payload = self.root / "payload.squashfs"
        subprocess.run(
            ["mksquashfs", str(appdir), str(payload), "-noappend", "-all-root",
             "-processors", "1", "-quiet"],
            check=True, capture_output=True,
        )
        self.image = self.root / "test application.AppImage"
        with self.image.open("wb") as output, payload.open("rb") as source:
            output.write(self.runtime)
            shutil.copyfileobj(source, output)
        self.image.chmod(0o755)

    def run_probe(self, args, exit_code=0):
        env = {key: value for key, value in os.environ.items()
               if not key.startswith(("APPIMAGE_", "URUNTIME_", "TARGET_APPIMAGE"))
               and key not in ("NO_CLEANUP", "NO_UNMOUNT", "FUSERMOUNT_PROG")}
        env["PROBE_EXIT_CODE"] = str(exit_code)
        command = [str(self.image), *args]
        if self.docker_image:
            command = [
                "docker", "run", "--rm", "--network", "none", "--cap-drop", "ALL",
                "--security-opt", "no-new-privileges",
                "--user", f"{os.getuid()}:{os.getgid()}",
                "-v", f"{self.root}:{self.root}", "-w", str(self.root),
                "-e", f"PROBE_EXIT_CODE={exit_code}",
                "--entrypoint", "/bin/sh", self.docker_image,
                "-c", 'test ! -e /dev/fuse && exec "$@"', "sh", *command,
            ]
        return subprocess.run(command, cwd=self.root, env=env, capture_output=True,
                              text=True, timeout=60)

    def test_plain_launch(self):
        result = self.run_probe([])
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.splitlines(), [str(self.image), str(self.root)])

    def test_arguments_and_exit_code(self):
        args = ["firmware with spaces.kdimg", "--example", "", "literal;$value"]
        result = self.run_probe(args, exit_code=23)
        self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
        self.assertEqual(result.stdout.splitlines(), [str(self.image), str(self.root), *args])

    def test_repack_rejects_invalid_payload(self):
        with self.image.open("r+b") as image:
            image.seek(len(self.runtime))
            image.write(b"nope")
        before = self.image.read_bytes()
        result = subprocess.run(["bash", str(REPACK), str(self.image)],
                                capture_output=True, text=True, timeout=10)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected a SquashFS AppImage", result.stderr)
        self.assertEqual(self.image.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
