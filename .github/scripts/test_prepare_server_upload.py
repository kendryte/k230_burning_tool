"""Validate workflow download and conversion to the download-server tree."""

import hashlib
import http.server
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tarfile
import tempfile
import threading
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
        responses = {}
        api_root = "https://api.github.com/repos/" + upload.DEFAULT_REPOSITORY
        assets = []
        for path in self.release.iterdir():
            url = api_root + "/releases/assets/" + path.name
            responses[url] = path.read_bytes()
            assets.append({"name": path.name, "size": path.stat().st_size, "apiUrl": url})
        artifacts = []
        for index, directory in enumerate(self.artifacts.iterdir(), 1):
            archive = io.BytesIO()
            with zipfile.ZipFile(archive, "w") as bundle:
                for path in directory.iterdir():
                    bundle.write(path, path.name)
            url = api_root + f"/actions/artifacts/{index}/zip"
            responses[url] = archive.getvalue()
            artifacts.append({"id": index, "name": directory.name, "expired": False,
                              "archive_download_url": url,
                              "digest": "sha256:" + hashlib.sha256(responses[url]).hexdigest()})
        artifacts.append({"id": 99, "name": "unrelated-build", "expired": False})

        def fake_gh(arguments, repo=None):
            commands.append((list(arguments), repo))
            if arguments[:2] == ["release", "view"]:
                return json.dumps({"tagName": self.version, "assets": assets,
                                   "apiUrl": api_root + "/releases/1"})
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
            if arguments[:2] == ["auth", "token"]:
                return "test-token"
            if arguments[0] == "api":
                self.assertIn("/actions/runs/123/artifacts", arguments[1])
                return json.dumps([{"artifacts": artifacts[:2]}, {"artifacts": artifacts[2:]}])
            self.fail("Unexpected gh command: " + str(arguments))

        downloaded = []

        def fake_open(request, timeout):
            downloaded.append(request.full_url)
            expected_accept = ("application/vnd.github+json" if "/actions/artifacts/" in
                               request.full_url else "application/octet-stream")
            self.assertEqual(request.get_header("Accept"), expected_accept)
            response = io.BytesIO(responses[request.full_url])
            response.status = 200
            response.headers = {"Content-Length": str(len(response.getvalue()))}
            return response

        with mock.patch.object(upload, "run_gh", side_effect=fake_gh), mock.patch.object(
                upload.urllib.request, "urlopen", side_effect=fake_open):
            with mock.patch.object(upload, "prepare", side_effect=KeyboardInterrupt):
                with self.assertRaises(KeyboardInterrupt):
                    upload.download_and_prepare(None, destination, output_format="directory")
            self.assertFalse(destination.exists())
            caches = list(self.root.glob(".k230-server-download.*"))
            self.assertEqual(len(caches), 1)
            first_downloads = list(downloaded)
            self.assertEqual(set(first_downloads), set(responses))
            downloaded.clear()
            upload.download_and_prepare("123", destination, output_format="directory")
            # Only the small checksum manifest is refreshed on restart.
            self.assertEqual(downloaded, [api_root + "/releases/assets/SHA256SUMS"])
        self.assertTrue((destination / self.version / "K230BurningTool_linux_arm64_normal_v2.2.6.zip").is_file())
        self.assertEqual({path.name for path in (caches[0] / "artifacts").iterdir()},
                         set(upload.REPOSITORY_ARTIFACTS.values()) | {upload.METADATA_ARTIFACT})
        self.assertTrue(all(repo == upload.DEFAULT_REPOSITORY for args, repo in commands
                            if args[0] in ("release", "run")))

    def test_cache_reuses_explicit_legacy_folder_and_rejects_different_run(self):
        cache = self.root / ".k230-server-download.old"
        shutil.copytree(self.release, cache / "release")
        package = next((cache / "release").glob("*.zip"))
        package.write_bytes(b"partial")
        self.assertEqual(upload.download_cache(cache, self.output, upload.DEFAULT_REPOSITORY,
                                               "123", self.version, self.commit), cache)
        self.assertEqual(package.read_bytes(), b"partial")
        with self.assertRaisesRegex(ValueError, "different release or workflow run"):
            upload.download_cache(cache, self.output, upload.DEFAULT_REPOSITORY,
                                  "124", self.version, self.commit)
        with self.assertRaisesRegex(ValueError, "must be separate"):
            upload.download_cache(cache, cache / "output", upload.DEFAULT_REPOSITORY,
                                  "123", self.version, self.commit)

    def test_cached_artifact_replaces_partial_extraction_and_rejects_traversal(self):
        archive = self.root / "artifact.zip"
        destination = self.root / "extracted"
        destination.mkdir()
        (destination / "partial").write_bytes(b"partial")
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("nested/complete", b"complete")
        upload.extract_artifact(archive, destination)
        self.assertFalse((destination / "partial").exists())
        self.assertEqual((destination / "nested/complete").read_bytes(), b"complete")
        with zipfile.ZipFile(archive, "w") as bundle:
            bundle.writestr("../outside", b"bad")
        with self.assertRaisesRegex(ValueError, "Unsafe archive path"):
            upload.extract_artifact(archive, destination)
        self.assertFalse((self.root / "outside").exists())

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


class ResumableDownloadTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.destination = Path(self.temp.name) / "asset"
        self.partial = self.destination.with_name("asset.part")
        self.payload = b"download payload\n" * 100000
        self.checksum = hashlib.sha256(self.payload).hexdigest()
        self.ranges = []
        self.authorizations = []
        self.interrupt = False
        self.ignore_range = False
        self.invalid_range = False
        fixture = self

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                fixture.authorizations.append(self.headers.get("Authorization"))
                if self.path == "/redirect":
                    self.send_response(302)
                    self.send_header("Location", fixture.url)
                    self.end_headers()
                    return
                value = self.headers.get("Range")
                fixture.ranges.append(value)
                offset = int(value.removeprefix("bytes=").removesuffix("-")) if value else 0
                total = len(fixture.payload)
                if offset == total:
                    self.send_response(416)
                    self.send_header("Content-Range", f"bytes */{total}")
                    self.end_headers()
                    return
                if fixture.ignore_range:
                    offset = 0
                self.send_response(206 if offset else 200)
                if offset:
                    start = offset + 1 if fixture.invalid_range else offset
                    self.send_header("Content-Range", f"bytes {start}-{total - 1}/{total}")
                self.send_header("Content-Length", str(total - offset))
                self.end_headers()
                if fixture.invalid_range:
                    return
                body = fixture.payload[offset:]
                if fixture.interrupt:
                    body = body[:1024 * 1024]
                self.wfile.write(body)

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()

        def stop_server():
            server.shutdown()
            server.server_close()
            thread.join()

        self.addCleanup(stop_server)
        self.url = f"http://127.0.0.1:{server.server_port}/asset"

    def download(self, size=True):
        upload.download_file(self.url, self.destination, "test-token",
                             len(self.payload) if size else None, self.checksum)

    def test_interrupted_transfer_is_kept_and_resumed_on_restart(self):
        self.interrupt = True
        with mock.patch.object(upload, "DOWNLOAD_ATTEMPTS", 1):
            with self.assertRaisesRegex(ValueError, "Rerun the same command"):
                self.download()
        offset = self.partial.stat().st_size
        self.assertEqual(offset, 1024 * 1024)
        self.assertFalse(self.destination.exists())
        self.interrupt = False
        self.download()
        self.assertEqual(self.ranges, [None, f"bytes={offset}-"])
        self.assertEqual(self.destination.read_bytes(), self.payload)
        self.assertFalse(self.partial.exists())
        self.download()
        self.assertEqual(len(self.ranges), 2)

    def test_retries_connection_failure_without_restarting_file(self):
        self.partial.write_bytes(self.payload[:100])
        real_open = upload.urllib.request.urlopen
        attempts = []

        def flaky_open(request, timeout):
            attempts.append(request.get_header("Range"))
            if len(attempts) == 1:
                raise OSError("connection reset")
            return real_open(request, timeout=timeout)

        with mock.patch.object(upload.urllib.request, "urlopen", side_effect=flaky_open), \
                mock.patch.object(upload.time, "sleep"):
            self.download()
        self.assertEqual(attempts, ["bytes=100-", "bytes=100-"])
        self.assertEqual(self.destination.read_bytes(), self.payload)

    def test_legacy_partial_file_resumes_and_range_ignored_restarts_safely(self):
        self.destination.write_bytes(self.payload[:123])
        self.download()
        self.assertEqual(self.ranges, ["bytes=123-"])
        self.assertEqual(self.destination.read_bytes(), self.payload)
        self.destination.write_bytes(self.payload[:321])
        self.ignore_range = True
        self.download()
        self.assertEqual(self.ranges[-1], "bytes=321-")
        self.assertEqual(self.destination.read_bytes(), self.payload)

    def test_corrupt_cached_file_is_replaced(self):
        self.destination.write_bytes(b"x" * len(self.payload))
        self.download()
        self.assertEqual(self.ranges, [None])
        self.assertEqual(self.destination.read_bytes(), self.payload)

    def test_corrupt_partial_is_redownloaded_after_checksum_failure(self):
        self.partial.write_bytes(b"wrong prefix")
        with mock.patch.object(upload.time, "sleep"):
            self.download()
        self.assertEqual(self.ranges, ["bytes=12-", None])
        self.assertEqual(self.destination.read_bytes(), self.payload)

    def test_redirect_preserves_range_without_forwarding_token(self):
        self.partial.write_bytes(self.payload[:123])
        upload.download_file(self.url.replace("/asset", "/redirect"), self.destination,
                             "test-token", len(self.payload), self.checksum)
        self.assertEqual(self.authorizations, ["Bearer test-token", None])
        self.assertEqual(self.ranges, ["bytes=123-"])
        self.assertEqual(self.destination.read_bytes(), self.payload)

    def test_completed_partial_with_unknown_size_handles_416(self):
        self.partial.write_bytes(self.payload)
        self.download(size=False)
        self.assertEqual(self.destination.read_bytes(), self.payload)
        self.assertFalse(self.partial.exists())

    def test_invalid_range_preserves_saved_bytes(self):
        self.partial.write_bytes(self.payload[:123])
        self.invalid_range = True
        with self.assertRaisesRegex(ValueError, "Invalid resume response"):
            self.download()
        self.assertEqual(self.partial.read_bytes(), self.payload[:123])
        self.assertFalse(self.destination.exists())


if __name__ == "__main__":
    unittest.main()
