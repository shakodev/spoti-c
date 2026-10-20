"""Convert the native route tests' raw RGBA captures to lossless PNGs."""
import argparse
from pathlib import Path
import re
import struct
import zlib


def chunk(kind, data):
    return (struct.pack('!I', len(data)) + kind + data
            + struct.pack('!I', zlib.crc32(kind + data) & 0xffffffff))


def convert(source, output):
    widths = {'narrow': 400, 'default': 1100, 'wide': 1440}
    pattern = r'(?:catalog-(?:search|album|artist|lyrics)|playlist)-(narrow|default|wide)-(96|120|144|192)\.rgba'
    count = 0
    for path in sorted(source.glob('*.rgba')):
        match = re.fullmatch(pattern, path.name)
        if not match:
            continue
        size, scale = match.groups()
        dpi = int(scale)
        width, height = widths[size] * dpi // 96, 720 * dpi // 96
        pixels = path.read_bytes()
        if len(pixels) != width * height * 4:
            raise ValueError(f'{path}: capture length does not match dimensions')
        stride = width * 4
        rows = b''.join(b'\0' + pixels[y * stride:(y + 1) * stride] for y in range(height))
        header = struct.pack('!IIBBBBB', width, height, 8, 6, 0, 0, 0)
        png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', header)
               + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))
        (output / path.with_suffix('.png').name).write_bytes(png)
        count += 1
    print(f'Converted {count} route captures.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    convert(args.source, args.output)
