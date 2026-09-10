# K230 Burning Tool Build Guide

[User README](README_en.md) | [简体中文 README](README.md)

This document is for developers and release maintainers. End users should use
the [user README](README_en.md) and a published package.

## Local release build

The release script builds only the normal variant; Avalon builds and publication
are temporarily disabled on all platforms. Avalon source and packaging support
remain available for future use. Windows
and Linux require Python 3.8+, `zip`, and native Qt IFW 4.11 tools in addition to
the existing Qt deployment tools. For example, on Linux x86_64:

```bash
git submodule update --init --recursive
bash .github/scripts/setup-ifw.sh linux x86_64 /tmp/k230-ifw-tools
export K230_IFW_TOOLS_DIR=/tmp/k230-ifw-tools/bin
./release.sh
```

The bootstrap script requires `curl`, `sha256sum`, and `7z`, downloads pinned
Qt IFW 4.11.0 archives, and verifies their SHA-256 before extraction. Use a new
destination directory. Supported combinations are `linux x86_64`, `linux arm64`,
and `windows x86_64`; `SEVENZIP` can select the 7-Zip executable. An existing
trusted IFW installation can instead supply `K230_IFW_TOOLS_DIR` directly.

Each Windows/Linux build produces a portable ZIP plus an IFW installer in
`build/ifw-artifacts/`. Raw build outputs retain individual `.sha256` files for
verification, but user downloads use consolidated checksum manifests. Portable
ZIPs contain the deployed `bin/`, libraries, and resources without IFW markers,
maintenance tools, or automatic desktop installation. There are no new AppImage
release artifacts. macOS retains signed/notarized DMGs and does not require IFW.

User-facing CI bundles contain only their installer/portable packages (or DMG)
and one `SHA256SUMS_<platform>_<arch>.txt` manifest. Tagged GitHub releases publish
eight packages: three installers, three portable ZIPs, and two signed macOS DMGs,
plus a single `SHA256SUMS`. Individual checksum sidecars and update-server files
are not attached to the public release.

`packaging/ifw/package.py` packages the deployed prefix without recompiling it.
The IFW payload places it under `app/` and adds `ifw-installation.json` at the
installation root. This marker binds the application layout to its local
Maintenance Tool; no executable path or update command is taken from the network.
The installer owns shortcut creation and removal. Retained Avalon support uses
separate installation directories, component IDs, and update repositories, but
the current release workflow produces only normal packages.

The default installation path is `~/Applications/K230BurningTool-Installed`,
selectable in the wizard. Old portable/AppImage installations are not migrated or deleted.
`CreateShortcuts=false` can be passed to IFW's CLI for deployments without shortcuts.

## Update repositories

Set the GitHub repository variable `K230_IFW_REPOSITORY_BASE` to a stable HTTPS
base URL, or export that environment variable for local release builds. No server
URL is assumed. With an empty value, installers are offline-only, repository
archives are not generated, and online updates are disabled in the application;
the Maintenance Tool can still uninstall.
With a URL, binarycreator creates hybrid installers: installation works offline,
then the Maintenance Tool uses the configured repository for subsequent updates.

The build appends `/<platform>/<arch>/<variant>` to the base URL, for example:

```text
<base>/linux/x86_64/normal/Updates.xml
<base>/linux/arm64/normal/Updates.xml
<base>/windows/x86_64/normal/Updates.xml
```

When configured, repository bundles and their checksums are generated separately
under `build/ifw-artifacts/repositories/`. CI uploads them as separate maintainer
`ifw-repository-<platform>-<arch>` artifacts, excluded from public package bundles.
Unpack the corresponding `K230BurningToolIFW_*_repository.tar.gz` into each
channel directory and publish its entire contents on the HTTPS server. Upload
versioned archives and metadata archives first, then publish `Updates.xml` last.
CI does not deploy that server. The legacy `k230_burningtool_lastest.txt` file is
retained in the separate `update-server-metadata` Actions artifact for deployment
to the existing version-check server, not published among user packages.
Keep channel URLs stable and bump the GUI project version for every update;
rebuilding the same IFW component version does not constitute an update.
Changing the repository base requires rebuilding installers or administering the
Maintenance Tool's repository settings. Protect repository publishing credentials.

Installed applications expose **Update Application** and **Manage Installation**
in the **Updates** menu.
Handoff asks for confirmation, refuses active burning jobs, and closes the app
before maintenance proceeds. IFW also requests that the application process be
closed before installing/updating/removing its component. Updates are interactive,
not silent background replacements. Portable ZIPs retain version checks and a
release-download link; they do not overwrite their own binaries.

## Installer tests

```bash
python3 -m unittest discover -s .github/scripts -p 'test_*.py'
qt-cmake -S tests/ifw-integration -B build/ifw-tests -G Ninja
cmake --build build/ifw-tests
ctest --test-dir build/ifw-tests --output-on-failure
K230_TEST_IFW_TOOLS="$K230_IFW_TOOLS_DIR" python3 .github/scripts/test_ifw_installer.py
```

The opt-in native IFW test installs a fixture, updates it from a local repository,
and uninstalls it in isolated temporary directories. Linux menu operations are
tested only when running unprivileged; Windows test shortcuts are disabled to
avoid touching the real desktop. Qt tests cover installed/portable detection,
missing tools, repository validation, menu handoff, and child-process environment.
Windows installers are not Authenticode-signed by this workflow; signing and
checksum regeneration should be added when publisher credentials are available.

## Windows builds

Windows builds run natively on `windows-2022` (x86_64), using MSYS2 `CLANG64`.
The Windows ARM64 matrix entry remains disabled. Qt, Clang,
winpthreads, and the C++ runtime come from matching MSYS2 packages. The install
hook runs windeployqt and collects transitive runtime DLL dependencies.
Wine is no longer used in CI. `release.sh` honors an explicit
`CMAKE_BUILD_PARALLEL_LEVEL`; CI uses two build jobs per runner.

## Linux builds

x86_64 retains the Qt 6.6 container on Ubuntu 22.04 and linuxdeployqt packaging.
ARM64 builds natively on `ubuntu-24.04-arm` with distribution Qt 6 development
packages and native linuxdeploy/Qt-plugin AppImages. Its packages therefore
have an Ubuntu 24.04 system-library baseline. Both architectures produce the
normal variant as portable ZIPs and IFW installers with checksums.
Set `K230_BURNING_LINUX_DEPLOY_TOOL=linuxdeploy` to use the ARM64 deployment
path locally; the default remains `linuxdeployqt`.

## macOS signing

macOS release artifacts require a Developer ID Application identity. The
certificate and private key must already be installed in the build account's
keychain.

```bash
security find-identity -v -p codesigning

MACOS_SIGN_IDENTITY="Developer ID Application: Example (TEAMID)" \
  ./release.sh
```

Optional variables:

- `MACOS_DEPLOYMENT_TARGET`: minimum macOS version (default `13.0`). It is
  passed to CMake and written into the app's `LSMinimumSystemVersion` field.
- `MACOS_KEYCHAIN`: absolute path to a dedicated keychain. Leave empty to use
  the account's normal keychain search list.
- `MACOS_KEYCHAIN_PASSWORD`: password used by CI to unlock the signing
  keychain. Store it as a secret in the `macos-signing` environment.
- `MACOS_NOTARY_PROFILE`: local `notarytool` keychain profile name. Leave empty
  to sign without notarization.

Create a notary profile on the signing Mac with `notarytool`, then pass its
profile name through `MACOS_NOTARY_PROFILE`:

```bash
xcrun notarytool store-credentials "K230Notary" \
  --apple-id "apple-id@example.com" \
  --team-id "TEAMID" \
  --password "app-specific-password"

MACOS_SIGN_IDENTITY="Developer ID Application: Example (TEAMID)" \
MACOS_NOTARY_PROFILE="K230Notary" \
  ./release.sh
```

Do not put Apple credentials, certificates, or private keys in the repository.

For local validation without a Developer ID identity, explicitly set
`MACOS_ALLOW_UNSIGNED=1`. The resulting app receives an ad-hoc signature after
deployment so its bundle is internally consistent, but Gatekeeper does not trust
it for distribution. Do not publish these local artifacts.

## Split macOS CI

Both x86_64 and ARM64 produce only the normal variant. Intel compilation
runs on GitHub's `macos-15-intel` runner. ARM64 compilation stays on the
self-hosted Mac (`self-hosted`, `macOS`, `ARM64`, `shenzhen_mac`) to reduce
hosted runner costs. Both architectures target macOS 13.0; the build runner
does not need to run that older OS. Build jobs have no signing secrets.

For branch and tag builds, `MACOS_BUILD_ONLY=1` deploys Qt into the apps and
archives the install trees as unsigned ZIP inputs. The self-hosted signing job
verifies their checksums and app architectures, then uses `SKIP_DEPLOYMENT=ON`
to sign and package them without running macdeployqt or compiling anything. It
notarizes and staples both the app and final DMG, validates both tickets, and
requires Gatekeeper assessment to accept both artifacts. The final checksums
are generated after stapling. Branch runs publish the signed/notarized DMGs as
workflow artifacts. Tag release upload selects those two DMGs plus final
Linux/Windows artifacts; unsigned ZIP inputs are excluded.

Configure all four secrets in this repository's `macos-signing` environment:
`MACOS_SIGN_IDENTITY`, `MACOS_KEYCHAIN` (explicit absolute path),
`MACOS_KEYCHAIN_PASSWORD`, and `MACOS_NOTARY_PROFILE`. The profile and signing
identity must exist in that keychain under the runner user. CMake and Xcode
command-line tools must be installed on the signing Mac. ARM64 compilation
and signing share a concurrency group within the repository.
The group uses `queue: max` so newer builds queue instead of replacing a pending
signing job. Collection verifies the build-time checksum sidecars before copying
only user packages into public bundles. Before release upload, all eight expected
packages must match their per-platform checksum manifests. The verifier then
writes the combined `SHA256SUMS`. Missing packages, stale duplicates, updater
bundles, dirty labels, unsigned inputs, and checksum failures stop publication.
Checksum files use LF line endings across platforms.

To re-enable Avalon, restore its invocation in `release.sh`, the workflow's
collection and signing steps, and the expected release artifacts/tests together.
Existing Avalon outputs are not deleted automatically; release validation rejects
them if they are mixed into a normal-only release directory.

Archive names include OS, architecture, variant, and revision. Local builds
can set `K230_BURNING_TARGET_ARCH` and `K230_BURNING_REVISION` to choose artifact
labels; those variables do not select a compiler architecture. macOS compilation
is selected by `MACOS_ARCHITECTURES` and other platforms by their native toolchain.
All CI jobs share `.github/scripts/release-revision.sh`: tag builds use the tag,
and branch builds use the first 12 commit-ID characters. Local builds use an exact
tag or short commit ID unless overridden. Tracked local modifications produce a
warning rather than adding `-dirty` to filenames; CI-generated workspace changes
therefore cannot alter the revision label.

## Qt on macOS

The macOS build jobs keep Qt outside the checkout, at
`$RUNNER_TOOL_CACHE/qt-6.6.3/Qt/6.6.3/macos`. It checks the installed version,
required tools, CMake package files, and platform plugin before reusing it.
When these checks pass, the Qt installer and its Python setup are skipped.
The Qt environment is activated on both installation and reuse paths.

On a missing or incomplete Qt installation, the hosted Intel runner uses
`actions/setup-python@v5` to select Python 3.12. This avoids Homebrew symlink
conflicts with the runner's existing python.org installation. The self-hosted
ARM64 runner continues to use Homebrew `python@3.12`. Both paths create an
isolated virtual environment and run `jurplel/install-qt-action@v4` with
`setup-python: false` and `cache: true`.
Change the macOS job's `QT_VERSION` to select a new version-specific directory.
No job cleanup removes this Qt installation; to force reinstallation, remove
only its version-specific directory on the runner before the next job.

Qt downloads use `.github/aqt.ini` to limit concurrent downloads, increase
network timeouts, and exclude the JAIST mirror after truncated downloads.
The workflow retries a failed installer once after 15 seconds; a second failure
stops the job. An installation-in-progress marker prevents future jobs from
reusing a partially installed tree. Successful installation and validation clear
the marker. Archive checksum verification remains enabled.

The self-hosted ARM64 builder reuses this directory across jobs; hosted Intel
builders use the action's remote cache between fresh runner instances.

Automated release configuration is maintained in
`.github/workflows/build.yml`. Keep workflow changes under maintainer review.

## Platform install hooks

The platform-specific install hooks validate the installed layout and package
the release:

- `gui/mac-install.cmake` signs and packages the macOS application and DMG.
- The Linux and Windows install hooks validate their runtime dependencies and
  packaging layout.
