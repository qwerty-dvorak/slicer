"""Generate procedural RGBA PNG images for testing purposes."""

import argparse
import logging
import math
import struct
import zlib
from pathlib import Path
from typing import IO

MAX_BYTE = 255
STRIPE_MODULO = 2

logger = logging.getLogger(__name__)

_TILE_COLOURS = (
    (255, 20, 20),
    (255, 200, 0),
    (20, 220, 70),
    (20, 120, 255),
    (220, 60, 220),
    (240, 240, 240),
)


def write_chunk(fp: IO[bytes], chunk_type: bytes, data: bytes) -> None:
    """Write a PNG chunk (length, type, data, CRC) to an open binary file."""
    fp.write(struct.pack(">I", len(data)))
    fp.write(chunk_type)
    fp.write(data)
    crc = zlib.crc32(chunk_type)
    crc = zlib.crc32(data, crc) & 0xFFFFFFFF
    fp.write(struct.pack(">I", crc))


def write_png_rgba(
    path: str | Path, width: int, height: int, pixels: bytearray
) -> None:
    """Write a raw RGBA pixel buffer to *path* as a valid PNG file."""
    if len(pixels) != width * height * 4:
        msg = "wrong pixel buffer size"
        raise ValueError(msg)

    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)  # filter: none
        start = y * stride
        raw.extend(pixels[start : start + stride])
    compressed = zlib.compress(bytes(raw), level=9)

    with Path(path).open("wb") as fp:
        fp.write(b"\x89PNG\r\n\x1a\n")
        ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
        write_chunk(fp, b"IHDR", ihdr)
        write_chunk(fp, b"IDAT", compressed)
        write_chunk(fp, b"IEND", b"")


def clamp_byte(v: float) -> int:
    """Clamp *v* to the range [0, 255] and return it as an integer."""
    if v < 0:
        return 0
    if v > MAX_BYTE:
        return MAX_BYTE
    return int(v)


def gen_checker_alpha(width: int, height: int) -> bytearray:
    """Return an RGBA buffer with a checker-board inside a soft-edged ring."""
    data = bytearray(width * height * 4)
    cx = width * 0.5
    cy = height * 0.5
    max_r = min(width, height) * 0.46
    hole_r = max_r * 0.36

    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 4
            c = 220 if ((x // 16 + y // 16) & 1) == 0 else 70
            data[i + 0] = c
            data[i + 1] = 160
            data[i + 2] = 30
            dx = x + 0.5 - cx
            dy = y + 0.5 - cy
            r = math.sqrt(dx * dx + dy * dy)
            if r > max_r:
                a = 0
            elif r < hole_r:
                a = 30
            else:
                t = (r - hole_r) / (max_r - hole_r)
                a = clamp_byte(MAX_BYTE * (1.0 - t))
            data[i + 3] = a
    return data


def gen_plasma_stripes(width: int, height: int) -> bytearray:
    """Return an RGBA buffer with sine-wave plasma colours and stripe alpha."""
    data = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 4
            fx = x / float(width)
            fy = y / float(height)
            r = 127 + 120 * math.sin(11.0 * fx + 6.0 * fy)
            g = 127 + 120 * math.sin(9.0 * fy + 10.0 * fx)
            b = 127 + 120 * math.sin(12.0 * fx + 13.0 * fy)
            a = (
                MAX_BYTE
                if (x // 10) % STRIPE_MODULO == 0
                else clamp_byte(70 + 160 * fy)
            )
            data[i + 0] = clamp_byte(r)
            data[i + 1] = clamp_byte(g)
            data[i + 2] = clamp_byte(b)
            data[i + 3] = a
    return data


def gen_pixel_art(width: int, height: int) -> bytearray:
    """Return an RGBA buffer with a tiled pixel-art pattern of six colours."""
    data = bytearray(width * height * 4)
    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 4
            tile = (x // 24 + y // 24) % len(_TILE_COLOURS)
            data[i + 0], data[i + 1], data[i + 2] = _TILE_COLOURS[tile]

            border = (x % 24 in (0, 23)) or (y % 24 in (0, 23))
            data[i + 3] = 120 if border else MAX_BYTE
    return data


def main() -> None:
    """Parse CLI arguments and write procedural PNG files to the output dir."""
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    parser = argparse.ArgumentParser(description="Generate procedural RGBA PNGs")
    parser.add_argument("--out", default="sample", help="output directory")
    args = parser.parse_args()

    Path(args.out).mkdir(parents=True, exist_ok=True)

    files = [
        ("proc_checker_alpha.png", 512, 512, gen_checker_alpha),
        ("proc_plasma_stripes.png", 640, 360, gen_plasma_stripes),
        ("proc_pixel_art.png", 384, 384, gen_pixel_art),
    ]

    for name, w, h, fn in files:
        path = Path(args.out) / name
        pixels = fn(w, h)
        write_png_rgba(path, w, h, pixels)
        logger.info("%s", path)


if __name__ == "__main__":
    main()
