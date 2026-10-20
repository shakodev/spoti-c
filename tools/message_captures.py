"""Losslessly convert the optional message tests' 32-bit BMP captures to PNG."""
import argparse
from pathlib import Path
import struct
import zlib


def chunk(kind, data):
    return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data) & 0xffffffff)


def convert(source, destination):
    data = source.read_bytes()
    assert data[:2] == b"BM"
    offset = struct.unpack_from("<I", data, 10)[0]
    width, signed_height, planes, bits, compression = struct.unpack_from("<iiHHI", data, 18)
    assert width > 0 and signed_height and planes == 1 and bits == 32 and compression == 0
    height = abs(signed_height)
    assert offset + width * height * 4 <= len(data)
    rows = []
    for y in range(height):
        row = y if signed_height < 0 else height - y - 1
        bgra = data[offset + row * width * 4:offset + (row + 1) * width * 4]
        rgb = bytearray(width * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = bgra[2::4], bgra[1::4], bgra[0::4]
        rows.append(b"\0" + rgb)
    header = struct.pack("!IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(b"".join(rows), 9)) + chunk(b"IEND", b"")
    destination.write_bytes(png)
    print(f"{destination}: {width} x {height}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("build", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    for source in sorted(args.build.glob("message-*.bmp")):
        convert(source, args.output / source.with_suffix(".png").name)
