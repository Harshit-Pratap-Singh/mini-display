#!/usr/bin/env python3
"""Prepare photos for the 240x240 display.

Everything becomes a 240x240 baseline JPEG, centre-cropped so nothing is letterboxed.
The device decodes with TJpg_Decoder, which cannot read progressive JPEG - the usual
reason a photo silently fails to appear - and there is no framebuffer, so a frame is
decoded in blocks straight to the panel.

    ./prepare_images.py holiday/*.jpg -o out/
    ./prepare_images.py --selftest

Animated GIFs are NOT supported: AnimatedGIF's decoder is 24,172 B against ~18 KB of
free heap on this board, so the firmware only accepts .jpg/.jpeg uploads. See
HARDWARE.md, "Milestone 5 stage B". Convert a GIF to stills yourself if you want a
frame from it.

Upload the results from the device's dashboard.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

SIZE = 240
# Comfortably inside a 2 MB filesystem while leaving room for a slideshow's worth.
DEFAULT_MAX_JPEG_BYTES = 45_000
DEFAULT_QUALITY = 80
MIN_QUALITY = 40

STILL_SUFFIXES = {".jpg", ".jpeg", ".png", ".bmp", ".webp", ".tif", ".tiff", ".heic", ".gif"}


def _pil():
    """Import Pillow late so --help still works without it installed."""
    try:
        from PIL import Image, ImageOps
    except ImportError:  # pragma: no cover - depends on the host
        sys.exit("Pillow is required for still images: pip install Pillow")
    return Image, ImageOps


def prepare_still(source: Path, target: Path, max_bytes: int, quality: int) -> int:
    """Centre-crop to square, resize to 240x240, save baseline JPEG. Returns bytes."""
    Image, ImageOps = _pil()
    with Image.open(source) as image:
        image = ImageOps.exif_transpose(image)  # honour the camera's rotation flag
        if image.mode in ("RGBA", "LA", "P"):
            # Flatten transparency onto black: the panel has no alpha and stray white
            # fringes are the usual result of ignoring this.
            image = image.convert("RGBA")
            flattened = Image.new("RGB", image.size, (0, 0, 0))
            flattened.paste(image, mask=image.split()[-1])
            image = flattened
        else:
            image = image.convert("RGB")

        # "cover" rather than "contain" - fill the panel, crop the overflow.
        square = ImageOps.fit(image, (SIZE, SIZE), method=Image.LANCZOS, centering=(0.5, 0.5))

        # Step the quality down until it fits; progressive=False is what TJpg needs.
        for attempt in range(quality, MIN_QUALITY - 1, -5):
            square.save(target, "JPEG", quality=attempt, optimize=True, progressive=False)
            size = target.stat().st_size
            if size <= max_bytes:
                return size
        return target.stat().st_size


def process(source: Path, out_dir: Path, args) -> tuple[Path, int, bool]:
    """Returns (target, size, over_budget). An animated GIF yields its first frame."""
    target = out_dir / f"{source.stem}.jpg"
    size = prepare_still(source, target, args.max_jpeg_bytes, args.quality)
    return target, size, size > args.max_jpeg_bytes


def selftest() -> int:
    """One runnable check: a synthetic image survives the pipeline as baseline JPEG."""
    Image, _ = _pil()
    with tempfile.TemporaryDirectory() as tmp:
        tmp_path = Path(tmp)
        # Deliberately non-square and with an alpha channel, to exercise crop and flatten.
        source = tmp_path / "wide.png"
        Image.new("RGBA", (800, 300), (200, 40, 40, 255)).save(source)

        target = tmp_path / "out.jpg"
        size = prepare_still(source, target, DEFAULT_MAX_JPEG_BYTES, DEFAULT_QUALITY)

        with Image.open(target) as result:
            assert result.size == (SIZE, SIZE), f"expected 240x240, got {result.size}"
            assert result.format == "JPEG", f"expected JPEG, got {result.format}"
            assert "progression" not in result.info, "TJpg_Decoder cannot read progressive JPEG"
            assert result.mode == "RGB", f"expected RGB, got {result.mode}"
        assert size <= DEFAULT_MAX_JPEG_BYTES, f"{size} B over the {DEFAULT_MAX_JPEG_BYTES} B budget"

        # A fully transparent source must flatten to black, not to white.
        clear = tmp_path / "clear.png"
        Image.new("RGBA", (240, 240), (255, 255, 255, 0)).save(clear)
        flat = tmp_path / "clear.jpg"
        prepare_still(clear, flat, DEFAULT_MAX_JPEG_BYTES, DEFAULT_QUALITY)
        with Image.open(flat) as result:
            assert result.getpixel((120, 120))[0] < 16, "transparent pixels should flatten to black"

    print("selftest OK: 240x240 baseline RGB JPEG, within budget, alpha flattened to black")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("inputs", nargs="*", type=Path, help="image files to convert")
    parser.add_argument("-o", "--out", type=Path, default=Path("prepared"),
                        help="output directory (default: ./prepared)")
    parser.add_argument("--quality", type=int, default=DEFAULT_QUALITY,
                        help=f"starting JPEG quality (default: {DEFAULT_QUALITY})")
    parser.add_argument("--max-jpeg-bytes", type=int, default=DEFAULT_MAX_JPEG_BYTES)
    parser.add_argument("--selftest", action="store_true", help="run the built-in check and exit")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if not args.inputs:
        parser.error("give at least one input file, or --selftest")

    args.out.mkdir(parents=True, exist_ok=True)
    total = 0
    over = 0
    for source in args.inputs:
        if not source.is_file():
            print(f"  skip {source}: not a file", file=sys.stderr)
            continue
        if source.suffix.lower() not in STILL_SUFFIXES:
            print(f"  skip {source}: unsupported type", file=sys.stderr)
            continue

        target, size, over_budget = process(source, args.out, args)
        total += size
        flag = "  OVER BUDGET" if over_budget else ""
        if over_budget:
            over += 1
        print(f"  {source.name} -> {target.name}  {size / 1024:.1f} KB{flag}")

    print(f"\n{total / 1024:.0f} KB total in {args.out}/")
    if over:
        print(f"{over} file(s) above budget - they will still upload, but they eat "
              f"into the ~2 MB filesystem.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
