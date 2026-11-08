#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Regenerates the raster icons from onetakelink.svg.

Needs rsvg-convert (librsvg) and Pillow:
    python3 assets/icon/make_icons.py
"""
import io, os, subprocess
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SVG = os.path.join(HERE, "onetakelink.svg")


def render(size):
    png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size), SVG],
                         check=True, capture_output=True).stdout
    return Image.open(io.BytesIO(png)).convert("RGBA")


def main():
    for size in (16, 32, 64, 128, 256, 512):
        render(size).save(os.path.join(HERE, f"onetakelink-{size}.png"), optimize=True)
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    big = render(256)
    # Each size rendered from the vector, not downscaled, for crisp small icons.
    frames = [render(s) for s in sizes]
    for name, size in (("check.png", 16), ("check@2x.png", 32)):
        png = subprocess.run(["rsvg-convert", "-w", str(size), "-h", str(size),
                              os.path.join(HERE, "check.svg")], check=True, capture_output=True).stdout
        open(os.path.join(HERE, name), "wb").write(png)
    big.save(os.path.join(HERE, "onetakelink.ico"), sizes=[(s, s) for s in sizes],
             append_images=frames)


if __name__ == "__main__":
    main()
