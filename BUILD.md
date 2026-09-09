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

The Windows CI job uses the Qt MinGW container, whose Windows compiler runs
under Wine. It sets `CMAKE_BUILD_PARALLEL_LEVEL=1` to limit simultaneous
compiler launches. `release.sh` passes a configured parallel level explicitly
to CMake; other builds retain the native default when it is unset or empty.

Wine process-start failures (such as `failed to map the shared user data`)
are separate from compiler diagnostics. Serial compilation is a mitigation,
not a guaranteed fix for Wine address-space conflicts. If these failures
persist, use a native Windows runner or investigate the container's Wine
runtime rather than changing the C source named in the failed command.

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

## Qt on macOS

The macOS workflow keeps Qt outside the checkout, at
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

Automated release configuration is maintained in
`.github/workflows/build.yml`. Keep workflow changes under maintainer review.

## Platform install hooks

The platform-specific install hooks validate the installed layout and package
the release:

- `gui/mac-install.cmake` signs and packages the macOS application and DMG.
- The Linux and Windows install hooks validate their runtime dependencies and
  packaging layout.
