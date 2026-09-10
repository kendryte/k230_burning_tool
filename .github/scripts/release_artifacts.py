#!/usr/bin/env python3
"""Collect verified user packages and validate the public release inventory."""

import argparse
import hashlib
from pathlib import Path
import re
import shutil

TARGETS = (("linux", "x86_64"), ("linux", "arm64"), ("windows", "x86_64"),
           ("macos", "x86_64"), ("macos", "arm64"))


def digest(path):
    result = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            result.update(block)
    return result.hexdigest()


def manifest_name(platform, arch):
    return "SHA256SUMS_" + platform + "_" + arch + ".txt"


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


def write_manifest(path, entries):
    path.write_bytes("".join(entries[name] + "  " + name + "\n" for name in sorted(entries)).encode("ascii"))


def select(directory, pattern, allow_unsigned=False):
    matches = list(directory.glob(pattern))
    if len(matches) != 1:
        raise ValueError("Expected exactly one package matching " + str(directory / pattern))
    path = matches[0]
    if (not path.is_file() or path.is_symlink() or "-dirty" in path.name or
            (not allow_unsigned and "unsigned" in path.name)):
        raise ValueError("Unsafe, unsigned, or dirty package: " + str(path))
    return path


def packages(directory, platform, arch, build_tree=False, allow_unsigned=False):
    if (platform, arch) not in TARGETS:
        raise ValueError("Unsupported publication target")
    extension = "dmg" if platform == "macos" else "zip"
    selected = [select(directory, f"K230BurningTool_{platform}_{arch}_normal_*.{extension}", allow_unsigned)]
    if platform != "macos":
        installer_dir = directory / "ifw-artifacts" if build_tree else directory
        extension = "exe" if platform == "windows" else "run"
        selected.append(select(installer_dir, f"K230BurningToolIFW_{platform}_{arch}_normal_*_setup.{extension}"))
    return selected


def collect(build_dir, output, platform, arch, allow_unsigned=False):
    selected = packages(build_dir, platform, arch, build_tree=True, allow_unsigned=allow_unsigned)
    entries = {}
    for path in selected:
        expected = read_manifest(path.with_name(path.name + ".sha256"))
        actual = digest(path)
        if expected != {path.name: actual}:
            raise ValueError("Checksum does not match package: " + str(path))
        entries[path.name] = actual
    output.mkdir(parents=True, exist_ok=True)
    manifest = output / manifest_name(platform, arch)
    for target in [manifest] + [output / path.name for path in selected]:
        if target.exists() or target.is_symlink():
            raise ValueError("Refusing to overwrite collected output: " + str(target))
    for path in selected:
        shutil.copy2(path, output / path.name)
    write_manifest(manifest, entries)


def verify(directory):
    expected_files = set()
    all_entries = {}
    for platform, arch in TARGETS:
        selected = packages(directory, platform, arch)
        manifest = directory / manifest_name(platform, arch)
        entries = read_manifest(manifest)
        if set(entries) != {path.name for path in selected}:
            raise ValueError("Checksum manifest does not identify the expected packages: " + manifest.name)
        for path in selected:
            if digest(path) != entries[path.name]:
                raise ValueError("Checksum mismatch: " + path.name)
        all_entries.update(entries)
        expected_files.update(entries)
        expected_files.add(manifest.name)
    actual_files = {path.name for path in directory.iterdir()}
    if actual_files - {"SHA256SUMS"} != expected_files:
        raise ValueError("Unexpected or missing release files: " + str(actual_files.symmetric_difference(expected_files) - {"SHA256SUMS"}))
    combined = directory / "SHA256SUMS"
    if combined.is_symlink() or (combined.exists() and not combined.is_file()):
        raise ValueError("Unsafe combined checksum manifest")
    write_manifest(combined, all_entries)
    print("Verified 8 user packages; generated SHA256SUMS")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    collector = commands.add_parser("collect")
    collector.add_argument("--build-dir", required=True, type=Path)
    collector.add_argument("--output", required=True, type=Path)
    collector.add_argument("--platform", required=True)
    collector.add_argument("--arch", required=True)
    collector.add_argument("--allow-unsigned", action="store_true")
    verifier = commands.add_parser("verify")
    verifier.add_argument("directory", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "collect":
            collect(args.build_dir, args.output, args.platform, args.arch, args.allow_unsigned)
        else:
            verify(args.directory)
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
