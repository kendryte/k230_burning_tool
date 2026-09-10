# K230 BurningTool

## Unreleased - 2026-09-09

### Added

- Added an English README alongside the Chinese README.
- Added signed macOS DMG artifacts with optional notarization support.
- Added SHA-256 checksum files for release artifacts.

### Changed

- Restricted release packages to authorized version-tag builds.
- Moved developer build, signing, and CI instructions from the user READMEs to
  `BUILD.md`.

### Fixed

- Prepared a compatible Python 3.12 environment for the Qt installer.


## V2.0.0 - 2024-09-19

### Features

1. **New `kburn` download protocol**  
   - Introduced to improve performance and reliability.

2. **Universal Loader**  
   - No dependencies on DRAM, enhancing compatibility across hardware.
