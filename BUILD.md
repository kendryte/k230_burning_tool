# K230 Burning Tool Build Guide

[User README](README_en.md) | [简体中文 README](README.md)

This document is for developers and release maintainers. End users should use
the [user README](README_en.md) and a published package.

## Local release build

The release script builds both the normal and Avalon Nano 3 variants:

```bash
git submodule update --init --recursive
./release.sh
```

The script runs the platform CMake install checks and generates a `.sha256`
checksum for every artifact. Linux releases include the Qt runtime in `.tar.gz`
archives and produce AppImages. An AppImage can run without FUSE with:

```bash
./K230BurningTool-x86_64.AppImage --appimage-extract-and-run
```

## Windows builds

Windows builds run natively on `windows-2022` (x86_64) and `windows-11-arm`
(ARM64), using MSYS2 `CLANG64` and `CLANGARM64` respectively. Qt, Clang,
winpthreads, and the C++ runtime come from matching MSYS2 packages. The install
hook runs windeployqt and collects transitive runtime DLL dependencies.
Wine is no longer used in CI. `release.sh` honors an explicit
`CMAKE_BUILD_PARALLEL_LEVEL`; CI uses two build jobs per runner.

## Linux builds

x86_64 retains the Qt 6.6 container on Ubuntu 22.04 and linuxdeployqt packaging.
ARM64 builds natively on `ubuntu-24.04-arm` with distribution Qt 6 development
packages and native linuxdeploy/Qt-plugin AppImages. Its packages therefore
have an Ubuntu 24.04 system-library baseline. Both architectures produce the
normal and Avalon variants as tarballs and AppImages with checksums.
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

For validation-only unsigned builds, explicitly set `MACOS_ALLOW_UNSIGNED=1`.
Do not use unsigned artifacts for distribution.

## Split macOS CI

Both x86_64 and ARM64 produce normal and Avalon variants. Intel compilation
runs on GitHub's `macos-15-intel` runner. ARM64 compilation stays on the
self-hosted Mac (`self-hosted`, `macOS`, `ARM64`, `shenzhen_mac`) to reduce
hosted runner costs. Both architectures target macOS 13.0; the build runner
does not need to run that older OS. Build jobs have no signing secrets.

For tags, `MACOS_BUILD_ONLY=1` deploys Qt into the apps and archives the install
trees as unsigned ZIP inputs. The self-hosted signing job verifies their
checksums and app architectures, then uses `SKIP_DEPLOYMENT=ON` to sign and
package them without running macdeployqt or compiling anything. It notarizes
and staples both the app and final DMG and validates both tickets. The final
checksums are generated after stapling. Release upload selects only the four
signed DMGs, plus final Linux/Windows artifacts; unsigned ZIP inputs are excluded.
Branch runs produce explicitly unsigned validation DMGs, not release assets.

Configure all four secrets in this repository's `macos-signing` environment:
`MACOS_SIGN_IDENTITY`, `MACOS_KEYCHAIN` (explicit absolute path),
`MACOS_KEYCHAIN_PASSWORD`, and `MACOS_NOTARY_PROFILE`. The profile and signing
identity must exist in that keychain under the runner user. CMake and Xcode
command-line tools must be installed on the signing Mac. ARM64 compilation
and signing share a concurrency group within the repository.
The group uses `queue: max` so newer builds queue instead of replacing a pending
signing job. Before release upload, all 16 expected packages (including Linux
AppImages) must exist with individual, matching checksum files. Missing variants,
stale duplicates, unsigned inputs, and checksum failures stop publication.
Checksum files use LF line endings across platforms.

Archive names include OS, architecture, variant, and revision. Local builds
can set `K230_BURNING_TARGET_ARCH` and `K230_BURNING_REVISION` to choose artifact
labels; those variables do not select a compiler architecture. macOS compilation
is selected by `MACOS_ARCHITECTURES` and other platforms by their native toolchain.

## Qt on macOS

The macOS build jobs keep Qt outside the checkout, at
`$RUNNER_TOOL_CACHE/qt-6.6.3/Qt/6.6.3/macos`. It checks the installed version,
required tools, CMake package files, and platform plugin before reusing it.
When these checks pass, the Qt installer and its Python setup are skipped.
The Qt environment is activated on both installation and reuse paths.

On a missing or incomplete installation, the workflow installs Homebrew
`python@3.12`, creates an isolated virtual environment, and runs
`jurplel/install-qt-action@v4` with `setup-python: false` and `cache: true`.
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
