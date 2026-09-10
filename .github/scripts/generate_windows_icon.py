#!/usr/bin/env python3
"""Package the K230 PNG icon set into a Windows ICO. Requires Pillow."""

import argparse
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCES = {
    16: "icon_16x16.png",
    32: "icon_32x32.png",
    64: "icon_32x32@2x.png",
    128: "icon_128x128.png",
    256: "icon_256x256.png",
}
SIZES = (16, 24, 32, 48, 64, 128, 256)


def generate(source_dir, output):
    images = {}
    for size, filename in SOURCES.items():
        with Image.open(Path(source_dir) / filename) as image:
            if image.size != (size, size):
                raise ValueError(f"{filename} must be {size}x{size}, got {image.size}")
            images[size] = image.convert("RGBA")

    # Keep supplied sizes intact; derive only the missing Windows-specific sizes.
    resampling = getattr(Image, "Resampling", Image)
    for size in SIZES:
        if size not in images:
            source = images[min(value for value in SOURCES if value >= size)]
            images[size] = source.resize((size, size), resampling.LANCZOS)
    images[256].save(output, format="ICO", sizes=[(size, size) for size in SIZES],
                     append_images=[images[size] for size in SIZES])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-dir", type=Path, default=ROOT / "gui/resources/icons")
    parser.add_argument("--output", type=Path, help="Defaults to icon.ico in the source directory")
    args = parser.parse_args()
    output = args.output or args.source_dir / "icon.ico"
    try:
        generate(args.source_dir, output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(f"Created {output} ({', '.join(str(size) for size in SIZES)} pixels)")


if __name__ == "__main__":
    main()
