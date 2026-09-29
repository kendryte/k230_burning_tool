"""Validate workflow download and conversion to the download-server tree."""

import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import unittest
from unittest import mock
import zipfile


SCRIPT = Path(__file__).with_name("prepare_server_upload.py")
spec = importlib.util.spec_from_file_location("prepare_server_upload", SCRIPT)
upload = importlib.util.module_from_spec(spec)
spec.loader.exec_module(upload)


class ServerUploadTests(unittest.TestCase):
    version = "v2.2.6"
    commit = "0123456789abcdef0123456789abcdef01234567"

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="server upload ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.release = self.root / "release"
        self.artifacts = self.root / "artifacts"
        self.output = self.root / "server"
        self.release.mkdir()
        self.artifacts.mkdir()
        self.create_public_release()
        self.create_repository_artifacts()
        metadata = self.artifacts / upload.METADATA_ARTIFACT
        metadata.mkdir()
        (metadata / "k230_burningtool_lastest.txt").write_text(json.dumps({
            "version": 2206,
            "hash": self.commit,
        }) + "\n")

    def write_manifest(self, path, entries):
        path.write_text("".join(
            f"{upload.digest(entry)}  {entry.name}\n" for entry in sorted(entries)
        ), encoding="ascii")

    def create_public_release(self):
        packages = []
        for platform, arch in upload.TARGETS:
            extension = "dmg" if platform == "macos" else "zip"
            package = self.release / f"K230BurningTool_{platform}_{arch}_normal_{self.version}.{extension}"
            package.write_bytes(package.name.encode())
            packages.append(package)
            if platform != "macos":
                extension = "exe" if platform == "windows" else "run"
                installer = self.release / (
                    f"K230BurningToolIFW_{platform}_{arch}_normal_{self.version}_setup.{extension}"
                )
                installer.write_bytes(installer.name.encode())
                packages.append(installer)
        self.write_manifest(self.release / "SHA256SUMS", packages)

    def repository_xml(self, platform, arch):
        component = "com.kendryte.k230burning." + ".".join(
            (platform, arch.replace("_", ""), "normal")
        )
        return component, f"""<Updates>
 <PackageUpdate>
  <Name>{component}</Name>
  <Version>{self.version[1:]}</Version>
  <DownloadableArchives>app.7z,content.7z</DownloadableArchives>
 </PackageUpdate>
</Updates>
"""

    def make_repository_archive(self, archive, platform, arch, unsafe=False):
        if unsafe:
            with tarfile.open(archive, "w:gz") as bundle:
                info = tarfile.TarInfo("../outside")
                info.size = 3
                bundle.addfile(info, io.BytesIO(b"bad"))
            return
        source = self.root / ("repository-" + platform + "-" + arch)
        source.mkdir()
        component, xml = self.repository_xml(platform, arch)
        (source / "Updates.xml").write_text(xml)
        (source / "2026-09-28-1200_meta.7z").write_bytes(b"metadata")
        component_dir = source / component
        component_dir.mkdir()
        for name in ("app.7z", "content.7z"):
            payload = component_dir / (self.version[1:] + name)
            payload.write_bytes(name.encode())
            payload.with_name(payload.name + ".sha1").write_text("0" * 40)
        (component_dir / (self.version[1:] + "meta.7z")).write_bytes(b"component metadata")
        with tarfile.open(archive, "w:gz") as bundle:
            bundle.add(source, arcname=".")

    def create_repository_artifacts(self):
        for (platform, arch), artifact_name in upload.REPOSITORY_ARTIFACTS.items():
            directory = self.artifacts / artifact_name
            directory.mkdir()
            name = f"K230BurningToolIFW_{platform}_{arch}_normal_{self.version}_repository.tar.gz"
            archive = directory / name
            self.make_repository_archive(archive, platform, arch)
            self.write_manifest(directory / (name + ".sha256"), [archive])

    def prepare(self, output=None):
        return upload.prepare(self.release, self.artifacts, output or self.output,
                              self.version, self.commit)

    def test_prepares_server_layout_and_md5_files(self):
        self.assertEqual(self.prepare(), self.version)
        release = self.output / self.version
        packages = [path for path in release.iterdir() if path.suffix != ".md5"]
        self.assertEqual(len(packages), 8)
        for package in packages:
            expected = hashlib.md5(package.read_bytes()).hexdigest()
            self.assertEqual((release / (package.name + ".md5")).read_text(),
                             f"{expected}  ./{package.name}\n")
        self.assertEqual(json.loads((self.output / "k230_burningtool_lastest.txt").read_text())["version"],
                         2206)
        for platform, arch in upload.REPOSITORY_ARTIFACTS:
            channel = self.output / platform / arch / "normal"
            self.assertTrue((channel / "Updates.xml").is_file())
            self.assertFalse(list(channel.glob("*_repository.tar.gz")))

    def test_corrupt_release_package_leaves_no_partial_output(self):
        package = next(self.release.glob("K230BurningTool_linux_arm64*.zip"))
        package.write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            self.prepare()
        self.assertFalse(self.output.exists())

    def test_unsafe_repository_path_is_rejected(self):
        platform, arch = "linux", "arm64"
        directory = self.artifacts / upload.REPOSITORY_ARTIFACTS[(platform, arch)]
        archive = next(directory.glob("*.tar.gz"))
        self.make_repository_archive(archive, platform, arch, unsafe=True)
        self.write_manifest(directory / (archive.name + ".sha256"), [archive])
        with self.assertRaisesRegex(ValueError, "Unsafe archive path"):
            self.prepare()
        self.assertFalse((self.root / "outside").exists())
        self.assertFalse(self.output.exists())

    def test_version_metadata_and_commit_must_match(self):
        with self.assertRaisesRegex(ValueError, "not requested version"):
            upload.prepare(self.release, self.artifacts, self.output, "v2.2.5", self.commit)
        with self.assertRaisesRegex(ValueError, "does not match"):
            upload.prepare(self.release, self.artifacts, self.output, self.version, "f" * 40)

    def test_existing_output_is_not_overwritten(self):
        self.output.mkdir()
        marker = self.output / "keep"
        marker.write_text("existing")
        with self.assertRaisesRegex(ValueError, "already exists"):
            self.prepare()
        self.assertEqual(marker.read_text(), "existing")

    def test_tar_archive_extracts_at_server_root_with_updates_last(self):
        output = self.root / "k230-burning-tool-server.tar.gz"
        upload.prepare_archive(self.release, self.artifacts, output, "tar.gz",
                               self.version, self.commit)
        with tarfile.open(output, "r:gz") as archive:
            names = archive.getnames()
            updates = [name for name in names if name.endswith("/Updates.xml")]
            self.assertEqual(names[-3:], sorted(updates))
            self.assertIn(self.version + "/K230BurningTool_linux_arm64_normal_v2.2.6.zip", names)
            self.assertIn("k230_burningtool_lastest.txt", names)
            self.assertFalse(any(name.startswith("server-upload/") for name in names))

    def test_zip_archive_has_root_relative_paths(self):
        output = self.root / "k230-burning-tool-server.zip"
        upload.prepare_archive(self.release, self.artifacts, output, "zip",
                               self.version, self.commit)
        with zipfile.ZipFile(output) as archive:
            names = archive.namelist()
            updates = [name for name in names if name.endswith("/Updates.xml")]
            self.assertEqual(names[-3:], sorted(updates))
            self.assertIn(self.version + "/K230BurningTool_windows_x86_64_normal_v2.2.6.zip",
                          names)
            self.assertIn("k230_burningtool_lastest.txt", names)

    def test_downloads_release_and_only_server_artifacts(self):
        destination = self.root / "downloaded-server"
        commands = []

        def fake_gh(arguments, repo=None):
            commands.append((list(arguments), repo))
            if arguments[:2] == ["release", "view"]:
                return json.dumps({"tagName": self.version})
            if arguments[:2] == ["run", "list"]:
                return json.dumps([{
                    "databaseId": 123,
                    "conclusion": "success",
                    "headBranch": self.version,
                    "headSha": self.commit,
                    "url": "https://example.invalid/run/123",
                }, {
                    "databaseId": 122,
                    "conclusion": "success",
                    "headBranch": "main",
                    "headSha": self.commit,
                }])
            if arguments[:2] == ["run", "view"]:
                return json.dumps({
                    "conclusion": "success",
                    "event": "push",
                    "headBranch": self.version,
                    "headSha": self.commit,
                    "url": "https://example.invalid/run/123",
                })
            target = Path(arguments[arguments.index("--dir") + 1])
            if arguments[:2] == ["release", "download"]:
                shutil.copytree(self.release, target, dirs_exist_ok=True)
                return ""
            names = [arguments[index + 1] for index, value in enumerate(arguments) if value == "--name"]
            for name in names:
                shutil.copytree(self.artifacts / name, target / name)
            return ""

        with mock.patch.object(upload, "run_gh", side_effect=fake_gh):
            upload.download_and_prepare(None, destination, output_format="directory")
        self.assertTrue((destination / self.version / "K230BurningTool_linux_arm64_normal_v2.2.6.zip").is_file())
        self.assertEqual(commands[2][0][:3], ["release", "download", self.version])
        downloaded_names = [commands[3][0][index + 1] for index, value in enumerate(commands[3][0])
                            if value == "--name"]
        self.assertEqual(set(downloaded_names), set(upload.REPOSITORY_ARTIFACTS.values()) |
                         {upload.METADATA_ARTIFACT})
        self.assertTrue(all(repo == upload.DEFAULT_REPOSITORY for _, repo in commands))

    def test_default_output_names(self):
        self.assertEqual(upload.default_output(self.version, "tar.gz"),
                         Path("k230-burning-tool-v2.2.6-server.tar.gz"))
        self.assertEqual(upload.default_output(self.version, "zip"),
                         Path("k230-burning-tool-v2.2.6-server.zip"))
        self.assertEqual(upload.default_output(self.version, "directory"),
                         Path("k230-burning-tool-v2.2.6-server"))

    def test_download_requires_successful_tag_run(self):
        for conclusion, branch, message in (
                ("failure", self.version, "did not complete successfully"),
                ("success", "main", "not for a vMAJOR.MINOR.PATCH tag")):
            with self.subTest(conclusion=conclusion, branch=branch), mock.patch.object(
                    upload, "run_gh", return_value=json.dumps({
                        "conclusion": conclusion,
                        "headBranch": branch,
                        "headSha": self.commit,
                    })):
                with self.assertRaisesRegex(ValueError, message):
                    upload.download_and_prepare("123", self.root / ("output-" + conclusion + branch))


if __name__ == "__main__":
    unittest.main()
