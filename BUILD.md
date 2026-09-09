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

The macOS workflow installs Homebrew `python@3.12`, creates an isolated virtual
environment, and runs `jurplel/install-qt-action@v4` with `setup-python: false`.
Keep this setup when updating the workflow because it avoids Qt installer
failures caused by the default Python version.

Automated release configuration is maintained in
`.github/workflows/build.yml`. Keep workflow changes under maintainer review.

## Platform install hooks

The platform-specific install hooks validate the installed layout and package
the release:

- `gui/mac-install.cmake` signs and packages the macOS application and DMG.
- The Linux and Windows install hooks validate their runtime dependencies and
  packaging layout.
