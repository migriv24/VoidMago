"""
make_icon.py — writes scripts/voidmago.ico, with no dependencies at all.

The motif is the interaction matrix, because that is what the application is:
a grid, its diagonal picked out (a project is not an interaction with itself),
and a handful of lit cells above and below it — mirrored, since the table is
symmetric and only the arrow flips.

Pure standard library (zlib + struct) on purpose. Void Mago's whole argument is
that the tool which repairs a broken install must not need anything installed
first, and its icon generator may as well hold the same line. Pillow would have
been one dependency for eighty lines of arithmetic.

    python scripts/make_icon.py
"""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

SIZES = (16, 32, 48, 64, 128, 256)

BG = (0x16, 0x14, 0x22)          # the ground
GRID = (0x2E, 0x2A, 0x44)        # empty cells: most of the table, and that is the point
DIAGONAL = (0x8A, 0x7A, 0xD8)    # the project itself
REQUIRES = (0xE0, 0x9A, 0x3C)    # a dependency edge
SEAM = (0x4E, 0xA8, 0xE0)        # data crosses, nothing links

# One quadrant of truth, mirrored across the diagonal — the same symmetry the
# matrix itself has. (row, col) with row < col; the reflection is generated.
UPPER = {
    (0, 2): REQUIRES,
    (0, 3): SEAM,
    (1, 3): REQUIRES,
    (2, 4): SEAM,
    (1, 4): REQUIRES,
}

N = 5  # cells per side


def cell_colour(row: int, col: int):
    if row == col:
        return DIAGONAL
    key = (row, col) if row < col else (col, row)
    return UPPER.get(key, GRID)


def render(size: int) -> bytes:
    """RGBA pixel rows for one square of the icon."""
    pad = max(1, size // 12)
    inner = size - 2 * pad
    gap = max(1, inner // 24)
    cell = (inner - gap * (N - 1)) // N
    used = cell * N + gap * (N - 1)
    origin = (size - used) // 2

    rows = []
    for y in range(size):
        row = bytearray()
        for x in range(size):
            colour = BG
            gx, gy = x - origin, y - origin
            if 0 <= gx < used and 0 <= gy < used:
                col, rx = divmod(gx, cell + gap)
                r, ry = divmod(gy, cell + gap)
                if rx < cell and ry < cell and col < N and r < N:
                    colour = cell_colour(r, col)
            row += bytes(colour) + b"\xff"
        rows.append(bytes(row))
    return rows


def png(size: int) -> bytes:
    raw = b"".join(b"\x00" + row for row in render(size))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)  # 8-bit RGBA
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico(path: Path) -> None:
    """An ICO is a directory of images; PNG-compressed entries are read by
    every Windows since Vista, which keeps this to a header and a loop."""
    images = [png(s) for s in SIZES]
    offset = 6 + 16 * len(images)
    out = bytearray(struct.pack("<HHH", 0, 1, len(images)))
    for size, blob in zip(SIZES, images):
        out += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    for blob in images:
        out += blob
    path.write_bytes(bytes(out))


if __name__ == "__main__":
    target = Path(__file__).resolve().parent / "voidmago.ico"
    ico(target)
    print(f"wrote {target}  ({target.stat().st_size} bytes, sizes {SIZES})")
