# K230 BurningTool

## 2.2.6 - 2026-09-28

### Added

- Added public OTP programming with a dedicated embedded OTP loader.
- Added optional device-side SHA-256 readback verification after burning.

### Changed

- Stream KDImage partitions directly from the source image instead of
  extracting temporary files, with 64-bit offset and size handling.
- Updated the embedded MMC, OTP, SPI NAND, and SPI NOR loaders.

### Fixed

- Fixed macOS builds with SDKs that no longer include the legacy AGL framework.
- Validate KDImage header and partition-table checksums, metadata, source
  hashes, and partition ranges before burning.
- Harden loader communication against stale USB responses, synchronization
  failures, invalid transfer ranges, and probe timeouts.
- Fixed USB monitor shutdown and hotplug lifecycle races, and continue device
  enumeration when an individual USB device is rejected.
- Cancel active burn jobs and long USB waits during GUI shutdown so the
  application exits promptly.
- Wake the Windows USB message loop before joining its polling thread so GUI
  shutdown cannot block indefinitely.

## 2.2.5 - 2026-09-10

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
