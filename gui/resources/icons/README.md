# K230 Application Icons

The PNG files and `icon.icns` are the supplied artwork. Qt uses the PNG sizes
and their `@2x` variants. Linux and IFW use `icon_256x256.png`; macOS uses
`icon.icns`. Windows uses `icon.ico`.

Regenerate the Windows icon after changing the PNG artwork:

```bash
python3 -m pip install Pillow
python3 .github/scripts/generate_windows_icon.py
```

The converter preserves the supplied 16, 32, 64, 128, and 256 pixel images and
derives 24 and 48 pixel entries from the next larger supplied size. It preserves
transparency. Pillow is needed only to regenerate the ICO, not to build the app.

Optional paths: `--source-dir PATH --output PATH`. Include the generated
`icon.ico` with changes to the PNG files.
