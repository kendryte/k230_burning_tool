#!/usr/bin/env python3
"""Create installers and update repositories from an already deployed application."""

import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile
from urllib.parse import urlsplit
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
SUPPORTED = {("linux", "x86_64"), ("linux", "arm64"), ("windows", "x86_64")}


def inside(path, root):
    try:
        return os.path.commonpath((str(path), str(root))) == str(root)
    except ValueError:
        return False


def xml_file(path, root):
    if hasattr(ET, "indent"):
        ET.indent(root)
    ET.ElementTree(root).write(path, encoding="utf-8", xml_declaration=True)


def element(root, name, text):
    ET.SubElement(root, name).text = str(text)


def repository_url(base, platform, arch, variant):
    if not base:
        return ""
    url = urlsplit(base)
    if (url.scheme != "https" or not url.hostname or url.username or url.password or
            url.query or url.fragment or any(c in base for c in "\r\n\t@")):
        raise ValueError("The repository base must be an HTTPS URL without credentials, query, or fragment")
    return base.rstrip("/") + "/" + "/".join((platform, arch, variant))


def prepare(source, work, platform, arch, variant, version, repository_base=""):
    if (platform, arch) not in SUPPORTED or variant not in ("normal", "avalon"):
        raise ValueError("Unsupported installer platform, architecture, or variant")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+(?:\.[0-9]+)?", version):
        raise ValueError("IFW version must be MAJOR.MINOR.PATCH[.BUILD]")
    source = Path(source).resolve()
    work = Path(work).resolve()
    if inside(work, source):
        raise ValueError("The packaging directory must be outside the deployed tree")
    avalon = variant == "avalon"
    app_id = "AvalonHomeSeriesFirmwareUpgradeTool" if avalon else "K230BurningTool"
    name = "Avalon Home Series Firmware Upgrade Tool" if avalon else "K230 Burning Tool"
    executable = "Avalon_Home_Series_Firmware_Upgrade_Tool.exe" if avalon and platform == "windows" else "K230BurningTool"
    if platform == "windows" and not avalon:
        executable += ".exe"
    if not (source / "bin" / executable).is_file():
        raise ValueError("The deployed application executable is missing")
    for path in source.rglob("*"):
        if path.is_symlink() and (not path.exists() or not inside(path.resolve(), source)):
            raise ValueError("Deployed symlink escapes the application tree: " + str(path))
    repository = repository_url(repository_base, platform, arch, variant)
    component = "com.kendryte.k230burning." + ".".join((platform, arch.replace("_", ""), variant))
    meta = work / "packages" / component / "meta"
    data = meta.parent / "data"
    config_dir = work / "config"
    meta.mkdir(parents=True)
    data.mkdir()
    config_dir.mkdir()
    shutil.copytree(source, data / "app", symlinks=True)
    shutil.copyfile(ROOT / "gui/resources" / ("icon_avalon.png" if avalon else "icons/icon_256x256.png"), data / "app-icon.png")
    details = {"schema": 1, "id": app_id, "name": name, "platform": platform,
               "arch": arch, "variant": variant, "version": version,
               "repository": repository, "executable": "app/bin/" + executable,
               "process": executable}
    (data / "ifw-installation.json").write_text(json.dumps(details, indent=2) + "\n", encoding="utf-8")
    script = "var K230 = " + json.dumps(details) + ";\n" + (ROOT / "packaging/ifw/installscript.qs").read_text()
    (meta / "installscript.qs").write_text(script, encoding="utf-8")
    package = ET.Element("Package")
    for key, value in {"DisplayName": name, "Description": name + " and bundled runtime libraries",
                       "Version": version, "ReleaseDate": datetime.date.today().isoformat(),
                       "Name": component, "Default": "true", "ForcedInstallation": "true",
                       "Script": "installscript.qs"}.items():
        element(package, key, value)
    xml_file(meta / "package.xml", package)
    config = ET.Element("Installer")
    for key, value in {"Name": name, "Version": version, "Title": name + " Setup", "Publisher": "Canaan",
                       "TargetDir": "@HomeDir@/Applications/" + app_id + "-Installed",
                       "StartMenuDir": name, "MaintenanceToolName": "MaintenanceTool",
                       "AllowNonAsciiCharacters": "true", "AllowSpaceInPath": "true",
                       "RunProgram": "@TargetDir@/app/bin/" + executable,
                       "RunProgramDescription": "Launch " + name,
                       "RemoveTargetDir": "false"}.items():
        element(config, key, value)
    if repository:
        remote = ET.SubElement(ET.SubElement(config, "RemoteRepositories"), "Repository")
        element(remote, "Url", repository)
    xml_file(config_dir / "config.xml", config)
    return details


def checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    path.with_name(path.name + ".sha256").write_bytes((digest.hexdigest() + "  " + path.name + "\n").encode("ascii"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "output", "tools", "platform", "arch", "variant", "version", "revision"):
        parser.add_argument("--" + name, required=True)
    parser.add_argument("--repository-base", default="")
    args = parser.parse_args()
    args.arch = {"aarch64": "arm64", "amd64": "x86_64"}.get(args.arch.lower(), args.arch.lower())
    output = Path(args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    tools = Path(args.tools).resolve()
    ext = ".exe" if args.platform == "windows" else ""
    required_tools = ["binarycreator", "installerbase"]
    if args.repository_base:
        required_tools.append("repogen")
    for tool in required_tools:
        if not (tools / (tool + ext)).is_file():
            parser.error("Missing Qt IFW tool: " + str(tools / (tool + ext)))
    revision = re.sub(r"[^A-Za-z0-9_.-]", "-", args.revision)
    base = "K230BurningToolIFW_" + "_".join((args.platform, args.arch, args.variant, revision))
    with tempfile.TemporaryDirectory(prefix="ifw-", dir=output) as temporary:
        work = Path(temporary)
        info = prepare(args.source, work, args.platform, args.arch, args.variant, args.version, args.repository_base)
        installer = work / (base + ("_setup.exe" if ext else "_setup.run"))
        subprocess.run([str(tools / ("binarycreator" + ext)),
                        "--hybrid" if info["repository"] else "--offline-only",
                        "-t", str(tools / ("installerbase" + ext)),
                        "-c", str(work / "config/config.xml"), "-p", str(work / "packages"), str(installer)], check=True)
        destination = output / installer.name
        installer.replace(destination)
        checksum(destination)
        print("Created " + str(destination))
        if info["repository"]:
            subprocess.run([str(tools / ("repogen" + ext)), "-p", str(work / "packages"), str(work / "repository")], check=True)
            repository = work / (base + "_repository.tar.gz")
            with tarfile.open(repository, "w:gz") as archive:
                archive.add(work / "repository", arcname=".")
            repository_output = output / "repositories"
            repository_output.mkdir(exist_ok=True)
            destination = repository_output / repository.name
            repository.replace(destination)
            checksum(destination)
            print("Created maintainer repository " + str(destination))


if __name__ == "__main__":
    main()
