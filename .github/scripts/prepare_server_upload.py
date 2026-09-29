#!/usr/bin/env python3
"""Download a release workflow's files and stage the download-server layout."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tarfile
import tempfile
import zipfile
import xml.etree.ElementTree as ET


TARGETS = (("linux", "x86_64"), ("linux", "arm64"), ("windows", "x86_64"),
           ("macos", "x86_64"), ("macos", "arm64"))
REPOSITORY_ARTIFACTS = {
    ("linux", "x86_64"): "ifw-repository-linux-x86_64",
    ("linux", "arm64"): "ifw-repository-linux-arm64",
    ("windows", "x86_64"): "ifw-repository-windows-x86_64",
}
METADATA_ARTIFACT = "update-server-metadata"
DEFAULT_REPOSITORY = "kendryte/k230_burning_tool"
DEFAULT_WORKFLOW = "build.yml"
VERSION_PATTERN = r"v[0-9]+\.[0-9]+\.[0-9]+"
MAX_ARCHIVE_FILES = 10000
MAX_ARCHIVE_BYTES = 4 * 1024 * 1024 * 1024


def digest(path, algorithm="sha256"):
    result = hashlib.new(algorithm)
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def read_manifest(path):
    if not path.is_file() or path.is_symlink():
        raise ValueError("Missing or unsafe checksum manifest: " + str(path))
    entries = {}
    for line in path.read_text(encoding="ascii").splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]{64})  ([^/\\]+)", line)
        if not match or match[2] in entries or match[2] in (".", ".."):
            raise ValueError("Invalid or duplicate checksum entry in " + str(path))
        entries[match[2]] = match[1].lower()
    if not entries:
        raise ValueError("Empty checksum manifest: " + str(path))
    return entries


def inside(path, root):
    try:
        return os.path.commonpath((str(path), str(root))) == str(root)
    except ValueError:
        return False


def find_one(directory, pattern, description):
    matches = [path for path in directory.rglob(pattern) if path.is_file()]
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one {description} matching {directory / pattern}")
    path = matches[0]
    if path.is_symlink() or not inside(path.resolve(), directory.resolve()):
        raise ValueError("Unsafe input file: " + str(path))
    return path


def normalize_version(value):
    if value and not value.startswith("v"):
        value = "v" + value
    if value and not re.fullmatch(VERSION_PATTERN, value):
        raise ValueError("Version must use vMAJOR.MINOR.PATCH")
    return value


def release_package_files(release_root):
    packages = []
    versions = set()
    for platform, arch in TARGETS:
        extension = "dmg" if platform == "macos" else "zip"
        portable = find_one(
            release_root,
            f"K230BurningTool_{platform}_{arch}_normal_*.{extension}",
            f"{platform} {arch} package",
        )
        selected = [portable]
        patterns = [re.compile(
            rf"K230BurningTool_{platform}_{arch}_normal_({VERSION_PATTERN})\.{extension}"
        )]
        if platform != "macos":
            extension = "exe" if platform == "windows" else "run"
            installer = find_one(
                release_root,
                f"K230BurningToolIFW_{platform}_{arch}_normal_*_setup.{extension}",
                f"{platform} {arch} installer",
            )
            selected.append(installer)
            patterns.append(re.compile(
                rf"K230BurningToolIFW_{platform}_{arch}_normal_({VERSION_PATTERN})_setup\.{extension}"
            ))
        for path, pattern in zip(selected, patterns):
            match = pattern.fullmatch(path.name)
            if not match:
                raise ValueError("Package name is not a tagged release artifact: " + path.name)
            versions.add(match[1])
            packages.append(path)

    if len({path.name for path in packages}) != len(packages):
        raise ValueError("Duplicate public package names")
    manifest = find_one(release_root, "SHA256SUMS", "release checksum manifest")
    entries = read_manifest(manifest)
    expected_names = {path.name for path in packages}
    if set(entries) != expected_names:
        raise ValueError("SHA256SUMS does not identify the expected release packages")
    for path in packages:
        if digest(path) != entries[path.name]:
            raise ValueError("Checksum mismatch: " + str(path))
    actual_names = {path.name for path in release_root.iterdir() if path.is_file()}
    if actual_names != expected_names | {manifest.name}:
        raise ValueError("Unexpected or missing GitHub Release assets: " +
                         str(actual_names.symmetric_difference(expected_names | {manifest.name})))
    return packages, versions


def verify_checksum_sidecar(path, sidecar):
    entries = read_manifest(sidecar)
    if entries != {path.name: digest(path)}:
        raise ValueError("Checksum does not match repository archive: " + str(path))


def safe_parts(name):
    if not name or "\\" in name or "\0" in name:
        raise ValueError("Unsafe archive path: " + repr(name))
    path = PurePosixPath(name)
    if path.is_absolute() or ".." in path.parts:
        raise ValueError("Unsafe archive path: " + name)
    return tuple(part for part in path.parts if part not in ("", "."))


def extract_repository(archive, destination):
    total = 0
    seen = set()
    with tarfile.open(archive, "r:gz") as bundle:
        members = bundle.getmembers()
        if len(members) > MAX_ARCHIVE_FILES:
            raise ValueError("Repository archive contains too many entries: " + str(archive))
        for member in members:
            parts = safe_parts(member.name)
            if not parts:
                continue
            if parts in seen:
                raise ValueError("Duplicate path in repository archive: " + member.name)
            seen.add(parts)
            if not (member.isfile() or member.isdir()):
                raise ValueError("Unsupported entry in repository archive: " + member.name)
            total += member.size
            if total > MAX_ARCHIVE_BYTES:
                raise ValueError("Repository archive expands beyond the safety limit: " + str(archive))
            target = destination.joinpath(*parts)
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            source = bundle.extractfile(member)
            if source is None:
                raise ValueError("Could not read repository entry: " + member.name)
            with source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
            target.chmod(member.mode & 0o777)


def validate_repository(directory, platform, arch, version):
    updates = directory / "Updates.xml"
    if not updates.is_file() or updates.is_symlink():
        raise ValueError("Repository is missing Updates.xml: " + str(directory))
    root = ET.parse(updates).getroot()
    package_updates = root.findall("PackageUpdate")
    if root.tag != "Updates" or len(package_updates) != 1:
        raise ValueError("Unexpected IFW repository metadata: " + str(updates))
    package = package_updates[0]
    component = "com.kendryte.k230burning." + ".".join(
        (platform, arch.replace("_", ""), "normal")
    )
    if package.findtext("Name") != component or package.findtext("Version") != version[1:]:
        raise ValueError("IFW repository component or version does not match its channel")
    component_dir = directory / component
    if not component_dir.is_dir() or component_dir.is_symlink():
        raise ValueError("IFW repository is missing its component directory")
    archives = [value.strip() for value in (package.findtext("DownloadableArchives") or "").split(",")]
    if not archives or any(not value for value in archives):
        raise ValueError("IFW repository has no downloadable archives")
    for name in archives:
        if not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
            raise ValueError("Unsafe downloadable archive name in Updates.xml")
        payload = component_dir / (version[1:] + name)
        checksum = payload.with_name(payload.name + ".sha1")
        if not payload.is_file() or not checksum.is_file():
            raise ValueError("IFW repository payload is incomplete: " + str(payload))
    if len(list(directory.glob("*_meta.7z"))) != 1:
        raise ValueError("IFW repository must contain exactly one metadata archive")


def install_repositories(artifact_root, output, version):
    for (platform, arch), artifact_name in REPOSITORY_ARTIFACTS.items():
        directory = artifact_root / artifact_name
        if not directory.is_dir() or directory.is_symlink():
            raise ValueError("Missing repository artifact: " + artifact_name)
        archive_name = f"K230BurningToolIFW_{platform}_{arch}_normal_{version}_repository.tar.gz"
        archive = find_one(directory, archive_name, f"{platform} {arch} repository archive")
        sidecar = find_one(directory, archive_name + ".sha256",
                           f"{platform} {arch} repository checksum")
        verify_checksum_sidecar(archive, sidecar)
        destination = output / platform / arch / "normal"
        destination.mkdir(parents=True)
        extract_repository(archive, destination)
        validate_repository(destination, platform, arch, version)


def metadata_file(artifact_root, version, expected_hash=None):
    directory = artifact_root / METADATA_ARTIFACT
    if not directory.is_dir() or directory.is_symlink():
        raise ValueError("Missing artifact: " + METADATA_ARTIFACT)
    path = find_one(directory, "k230_burningtool_lastest.txt", "legacy update metadata")
    try:
        metadata = json.loads(path.read_text(encoding="utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("Invalid legacy update metadata: " + str(path)) from error
    major, minor, patch = (int(value) for value in version[1:].split("."))
    expected_value = major * 1000 + minor * 100 + patch
    commit = str(metadata.get("hash", "")) if isinstance(metadata, dict) else ""
    if (not isinstance(metadata, dict) or metadata.get("version") != expected_value or
            not re.fullmatch(r"[0-9a-f]{40}", commit) or
            (expected_hash and commit != expected_hash)):
        raise ValueError("Legacy update metadata does not match " + version)
    return path


def write_md5(path):
    sidecar = path.with_name(path.name + ".md5")
    sidecar.write_text(f"{digest(path, 'md5')}  ./{path.name}\n", encoding="ascii")


def validate_input_directory(path, description):
    path = Path(os.path.abspath(Path(path).expanduser()))
    if not path.is_dir() or path.is_symlink():
        raise ValueError(description + " must be a real directory: " + str(path))
    return path.resolve()


def validate_output(output, inputs=()):
    output = Path(os.path.abspath(Path(output).expanduser()))
    if output.exists() or output.is_symlink():
        raise ValueError("Output already exists: " + str(output))
    output = output.resolve()
    for path in inputs:
        if inside(output, path):
            raise ValueError("Output must be outside the downloaded input directories")
    return output


def prepare(release_root, artifact_root, output, requested_version=None, expected_hash=None,
            announce=True):
    release_root = validate_input_directory(release_root, "GitHub Release input")
    artifact_root = validate_input_directory(artifact_root, "Workflow artifact input")
    output = validate_output(output, (release_root, artifact_root))
    requested_version = normalize_version(requested_version)

    packages, versions = release_package_files(release_root)
    if len(versions) != 1:
        raise ValueError("Release packages do not use one version: " + ", ".join(sorted(versions)))
    version = next(iter(versions))
    if requested_version and version != requested_version:
        raise ValueError(f"Release contains {version}, not requested version {requested_version}")
    metadata = metadata_file(artifact_root, version, expected_hash)

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="." + output.name + ".", dir=output.parent) as temporary:
        staging = Path(temporary) / "server-upload"
        release = staging / version
        release.mkdir(parents=True)
        for package in packages:
            destination = release / package.name
            shutil.copy2(package, destination)
            write_md5(destination)
        install_repositories(artifact_root, staging, version)
        shutil.copy2(metadata, staging / metadata.name)
        staging.replace(output)
    if announce:
        print(f"Prepared {version} server upload in {output}")
        print(f"Copied {len(packages)} GitHub Release packages and generated MD5 sidecars")
        print(f"Expanded {len(REPOSITORY_ARTIFACTS)} IFW update repositories")
    return version


def ordered_archive_paths(root):
    paths = list(root.rglob("*"))
    if any(path.is_symlink() or not (path.is_file() or path.is_dir()) for path in paths):
        raise ValueError("Server upload tree contains an unsupported file type")
    directories = sorted((path for path in paths if path.is_dir()),
                         key=lambda path: path.relative_to(root).as_posix())
    updates = sorted((path for path in paths if path.is_file() and path.name == "Updates.xml"),
                     key=lambda path: path.relative_to(root).as_posix())
    files = sorted((path for path in paths if path.is_file() and path.name != "Updates.xml"),
                   key=lambda path: path.relative_to(root).as_posix())
    return directories + files + updates


def create_upload_archive(root, destination, archive_format):
    if archive_format not in ("tar.gz", "zip"):
        raise ValueError("Archive format must be tar.gz or zip")
    paths = ordered_archive_paths(root)
    if archive_format == "tar.gz":
        with tarfile.open(destination, "w:gz") as archive:
            for path in paths:
                archive.add(path, arcname=path.relative_to(root).as_posix(), recursive=False)
    else:
        with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED,
                             allowZip64=True) as archive:
            for path in paths:
                if path.is_file():
                    archive.write(path, path.relative_to(root).as_posix())


def prepare_archive(release_root, artifact_root, output, archive_format,
                    requested_version=None, expected_hash=None):
    release_root = validate_input_directory(release_root, "GitHub Release input")
    artifact_root = validate_input_directory(artifact_root, "Workflow artifact input")
    output = validate_output(output, (release_root, artifact_root))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".k230-server-archive.", dir=output.parent) as temporary:
        root = Path(temporary)
        tree = root / "tree"
        version = prepare(release_root, artifact_root, tree, requested_version, expected_hash,
                          announce=False)
        archive = root / ("upload." + archive_format)
        create_upload_archive(tree, archive, archive_format)
        archive.replace(output)
    print(f"Prepared {version} server upload archive in {output}")
    print("Archive paths are relative to the k230_burningtool server root")
    return version


def run_gh(arguments, repo=None):
    if shutil.which("gh") is None:
        raise ValueError("GitHub CLI (gh) is required")
    command = ["gh"] + [str(value) for value in arguments]
    if repo:
        command.extend(("--repo", repo))
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        message = result.stderr.strip() or result.stdout.strip() or "GitHub CLI command failed"
        raise ValueError(message)
    return result.stdout


def gh_json(arguments, repo, description):
    try:
        return json.loads(run_gh(arguments, repo))
    except json.JSONDecodeError as error:
        raise ValueError("GitHub CLI returned invalid " + description) from error


def latest_release_version(repo):
    release = gh_json(["release", "view", "--json", "tagName"], repo,
                      "release metadata")
    try:
        version = normalize_version(release.get("tagName"))
        if not version:
            raise ValueError
    except (AttributeError, ValueError) as error:
        raise ValueError("Latest GitHub Release does not use vMAJOR.MINOR.PATCH") from error
    return version


def find_release_run(version, repo):
    runs = gh_json([
        "run", "list", "--workflow", DEFAULT_WORKFLOW, "--event", "push",
        "--status", "success", "--limit", "100", "--json",
        "databaseId,conclusion,headBranch,headSha,url,createdAt",
    ], repo, "workflow run list")
    if not isinstance(runs, list):
        raise ValueError("GitHub CLI workflow run list is not an array")
    matches = [run for run in runs if run.get("headBranch") == version]
    if not matches:
        raise ValueError(f"No successful {DEFAULT_WORKFLOW} run found for {version}")
    return matches[0]


def resolve_workflow_run(run_id, repo, requested_version=None):
    requested_version = normalize_version(requested_version)
    if run_id is None:
        version = requested_version or latest_release_version(repo)
        run = find_release_run(version, repo)
        run_id = str(run.get("databaseId", ""))
    else:
        run_id = str(run_id)
        if not re.fullmatch(r"[1-9][0-9]*", run_id):
            raise ValueError("Run ID must be a positive integer")
        run = gh_json([
            "run", "view", run_id, "--json", "conclusion,event,headBranch,headSha,url"
        ], repo, "workflow run metadata")
    if run.get("conclusion") != "success":
        raise ValueError("Workflow run did not complete successfully: " + str(run.get("conclusion")))
    try:
        version = normalize_version(run.get("headBranch"))
        if not version:
            raise ValueError
    except ValueError as error:
        raise ValueError("Workflow run is not for a vMAJOR.MINOR.PATCH tag") from error
    if requested_version and requested_version != version:
        raise ValueError(f"Workflow run is for {version}, not requested version {requested_version}")
    if not re.fullmatch(r"[1-9][0-9]*", run_id):
        raise ValueError("Workflow run has an invalid run ID")
    head_sha = str(run.get("headSha", ""))
    if not re.fullmatch(r"[0-9a-f]{40}", head_sha):
        raise ValueError("Workflow run has an invalid head commit")
    return run_id, version, head_sha


def default_output(version, output_format):
    base = "k230-burning-tool-" + version + "-server"
    if output_format == "directory":
        return Path(base)
    if output_format in ("tar.gz", "zip"):
        return Path(base + "." + output_format)
    raise ValueError("Output format must be directory, tar.gz, or zip")


def download_and_prepare(run_id=None, output=None, repo=DEFAULT_REPOSITORY,
                         requested_version=None, output_format="tar.gz"):
    run_id, version, head_sha = resolve_workflow_run(run_id, repo, requested_version)
    output = validate_output(output or default_output(version, output_format))
    if output_format not in ("directory", "tar.gz", "zip"):
        raise ValueError("Output format must be directory, tar.gz, or zip")

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".k230-server-download.", dir=output.parent) as temporary:
        root = Path(temporary)
        release_root = root / "release"
        artifact_root = root / "artifacts"
        release_root.mkdir()
        artifact_root.mkdir()
        print(f"Downloading GitHub Release {version}", flush=True)
        run_gh(["release", "download", version, "--dir", release_root], repo)
        print(f"Downloading server artifacts from workflow run {run_id}", flush=True)
        command = ["run", "download", run_id, "--dir", artifact_root]
        for name in list(REPOSITORY_ARTIFACTS.values()) + [METADATA_ARTIFACT]:
            command.extend(("--name", name))
        run_gh(command, repo)
        if output_format != "directory":
            return prepare_archive(release_root, artifact_root, output, output_format,
                                   version, head_sha)
        return prepare(release_root, artifact_root, output, version, head_sha)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_id", nargs="?",
                        help="Tag workflow run ID; defaults to the latest release's successful run")
    parser.add_argument("-o", "--output", type=Path,
                        help="Output path; defaults to a versioned filename")
    parser.add_argument("--repo", default=DEFAULT_REPOSITORY,
                        help="GitHub OWNER/REPO (default: %(default)s)")
    parser.add_argument("--version", help="Release version to select or verify")
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--zip", action="store_const", const="zip", dest="output_format",
                        help="Create a ZIP instead of the default tar.gz")
    output.add_argument("--directory", action="store_const", const="directory",
                        dest="output_format", help="Create an unpacked directory")
    parser.set_defaults(output_format="tar.gz")
    args = parser.parse_args()
    try:
        download_and_prepare(args.run_id, args.output, args.repo, args.version,
                             args.output_format)
    except (ValueError, OSError, tarfile.TarError, zipfile.BadZipFile, ET.ParseError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
