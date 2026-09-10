# K230 Burning Tool

[English](README_en.md) | [简体中文](README.md)

K230 Burning Tool is a desktop application for downloading firmware to
Kendryte K230 and K230D boards.

## 1. Install the driver

If the tool cannot detect the board, check whether Device Manager shows an
unknown `K230 USB Boot Device`:

![](docs/driver_no_valid_driver.png)

If it is unknown, use [Zadig](https://zadig.akeo.ie/) to install the driver for
`K230 USB Boot Device`:

![](docs/zadig_intall.png)

## 2. Burning workflow

1. **Select the image**: select the image to burn. The tool supports `*.kdimg`
   and other formats such as `*.img` and `*.bin`.
2. **Select the target medium**: choose the storage interface used by the board.
3. **Connect the board**: hold the board's `BOOT` button to enter `BootROM`
   mode, then connect the board.
4. **Start burning**: click **Start** to begin downloading the image.
5. **Confirm completion**: click **Confirm** after burning finishes before
   selecting another image or starting another burn.

### Image download

Select an image that matches the target device, such as `*.img` or `*.kdimg`:

![](docs/image_download.png)

> A `*.kdimg` file contains partition information, so individual partitions can
> be selected for download.

## 3. Download options

![](docs/burn_control.png)

### Target medium

K230 supports five storage media types:

![](docs/medium.png)

- **EMMC**: eMMC or an SD card connected to the `K230 SDIO0` interface.
- **SD card**: eMMC or an SD card connected to the `K230 SDIO1` interface.
- **SPI NAND**: NAND flash connected to the `SPI` bus.
- **SPI NOR**: NOR flash connected to the `SPI` bus.
- **OTP**: the chip's built-in OTP device.

### Start

After clicking **Start**, the tool searches for and waits for a connected
`K230 USB Boot Device`.

> Burning behavior can vary by medium. Adjust the options for the selected
> device and storage type.

### Confirm

Click **Confirm** after burning completes to start another burn:

![](docs/how_to_confirm.png)

### Tip

Some boards do not expose a `BOOT` button. In that case, remove the storage
device before powering on and insert it after power-on to enter `BootROM` mode.

## 4. Release packages

Official releases contain the application packages and one `SHA256SUMS` checksum
list. CI downloads contain a corresponding per-platform checksum list. Download
the package for your platform; individual checksum sidecars and updater-server
bundles are kept out of user downloads. Filenames use a release tag or short
commit ID, without a `dirty` suffix.

Windows and Linux offer two editions of the normal variant. Avalon builds are
temporarily disabled:

- **Installer:** choose the Qt IFW `_setup.exe` (Windows) or `_setup.run` (Linux).
  The wizard installs the application, bundled libraries, shortcuts, and a
  Maintenance Tool. The default location is `~/Applications/K230BurningTool-Installed`.
  Linux `.run` files must be executable.
- **Portable ZIP:** extract the complete archive and launch the executable in
  `bin/`. Keep all libraries and subdirectories together. It does not install
  desktop shortcuts or display installation prompts. Linux users may need to
  restore executable permission on `bin/K230BurningTool` after extraction.

For the installed edition, **Updates > Update Application** opens the
Maintenance Tool after confirmation and closes the application. Finish or cancel
all burning jobs first. Online updates require a release configured with a hosted
repository; otherwise the menu reports that updates are not configured.
**Updates > Manage Installation** lets you manage or uninstall the application
independently of online updates.

Portable editions provide **Check for Updates** and **Download Releases** in the
**Updates** menu. These manual actions remain available when automatic checks are
disabled. Portable ZIPs are updated by extracting a new release into a new directory.
macOS keeps its signed and notarized drag-and-drop DMG. New releases no longer
produce AppImages. Existing AppImage installations and manually created shortcuts
are not deleted automatically; remove them separately when switching editions.

Update-server bundles are generated only when updates are configured and are
uploaded separately for maintainers, not attached to the public release.

Developers who need to build or publish a release should read the
[build and release guide](BUILD.md).
