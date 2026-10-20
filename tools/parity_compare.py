"""Export native RGBA captures and compare equal-scale pixel crops without resampling.

Input metadata must state physical width, height and dpi. Reference pixels must
come from an actual reference capture, never from a generated mockup. This tool
reports differences; it does not decide that a screen has achieved parity.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib


def png_chunk(kind, data):
    return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))


def write_png(path, width, height, rgba):
    rows = b"".join(b"\0" + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))
    header = struct.pack("!IIBBBBB", width, height, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header) +
                     png_chunk(b"IDAT", zlib.compress(rows)) + png_chunk(b"IEND", b""))


def read_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Not a PNG file")
    offset, packed, header = 8, bytearray(), None
    while offset < len(data):
        length = struct.unpack_from("!I", data, offset)[0]
        kind, body = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + length]
        if len(body) != length or offset + length + 12 > len(data):
            raise ValueError("Truncated PNG")
        crc = struct.unpack_from("!I", data, offset + 8 + length)[0]
        if zlib.crc32(kind + body) != crc:
            raise ValueError("PNG CRC mismatch")
        if kind == b"IHDR":
            header = struct.unpack("!IIBBBBB", body)
        elif kind == b"IDAT":
            packed.extend(body)
        offset += length + 12
        if kind == b"IEND":
            break
    if header is None:
        raise ValueError("Missing PNG header")
    width, height, bits, color, compression, filtering, interlace = header
    if bits != 8 or color not in (2, 6) or compression or filtering or interlace:
        raise ValueError("Use noninterlaced 8-bit RGB/RGBA PNG")
    if not 0 < width <= 16384 or not 0 < height <= 16384:
        raise ValueError("Unsupported image size")
    channels = 4 if color == 6 else 3
    stride = width * channels
    raw = zlib.decompress(packed)
    if len(raw) != height * (stride + 1):
        raise ValueError("Unexpected PNG pixel length")
    previous, rgba = bytearray(stride), bytearray()
    for y in range(height):
        start = y * (stride + 1)
        mode, row = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        if mode > 4:
            raise ValueError("Unknown PNG filter")
        for x in range(stride):
            left = row[x - channels] if x >= channels else 0
            up, diagonal = previous[x], previous[x - channels] if x >= channels else 0
            predictor = 0
            if mode == 1:
                predictor = left
            elif mode == 2:
                predictor = up
            elif mode == 3:
                predictor = (left + up) // 2
            elif mode == 4:
                p = left + up - diagonal
                distances = (abs(p - left), abs(p - up), abs(p - diagonal))
                predictor = (left, up, diagonal)[distances.index(min(distances))]
            row[x] = (row[x] + predictor) & 255
        if channels == 4:
            rgba.extend(row)
        else:
            for x in range(0, stride, 3):
                rgba.extend(row[x:x + 3] + b"\xff")
        previous = row
    return width, height, bytes(rgba)


def load(path, metadata):
    info = json.loads(metadata.read_text(encoding="utf-8"))
    width, height = info["width"], info["height"]
    if not 0 < width <= 16384 or not 0 < height <= 16384 or info["dpi"] not in (96, 120, 144, 192):
        raise ValueError("Capture dimensions/DPI are invalid")
    if path.suffix.lower() == ".png":
        w, h, pixels = read_png(path)
        if (w, h) != (width, height):
            raise ValueError("Metadata disagrees with PNG dimensions")
    else:
        pixels = path.read_bytes()
    if len(pixels) != width * height * 4:
        raise ValueError("RGBA byte count does not match metadata")
    return info, pixels


def check_output_paths(output, inputs, comparing):
    names = ["native.png"]
    if comparing:
        names += ["reference.png", "comparison.png", "difference.png", "comparison.json"]
    for name in names:
        target = output / name
        for source in inputs:
            if source is not None and (target.resolve() == source.resolve() or
                                       (target.exists() and source.samefile(target))):
                raise ValueError(f"Output would overwrite an input: {target}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--reference", type=Path)
    parser.add_argument("--reference-metadata", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    info, native = load(args.native, args.metadata)
    width, height = info["width"], info["height"]
    if args.reference is not None:
        if args.reference_metadata is None:
            raise ValueError("Reference metadata is required for comparisons")
        ref_info, reference = load(args.reference, args.reference_metadata)
        if any(info[k] != ref_info[k] for k in ("width", "height", "dpi")):
            raise ValueError("Compare identical pixel crop sizes and DPI; no implicit resize")
    check_output_paths(args.output,
                       [args.native, args.metadata, args.reference, args.reference_metadata],
                       args.reference is not None)
    args.output.mkdir(parents=True, exist_ok=True)
    write_png(args.output / "native.png", width, height, native)
    if args.reference is None:
        return
    rows, difference, changed, total = bytearray(), bytearray(), 0, 0
    changed_alpha, total_alpha = 0, 0
    for y in range(height):
        start, end = y * width * 4, (y + 1) * width * 4
        rows.extend(reference[start:end] + native[start:end])
    for i in range(0, len(native), 4):
        delta = [abs(native[i + c] - reference[i + c]) for c in range(3)]
        alpha = abs(native[i + 3] - reference[i + 3])
        changed += any(delta) or alpha != 0
        changed_alpha += alpha != 0
        total += sum(delta)
        total_alpha += alpha
        # Show alpha-only differences as grayscale in the opaque heatmap.
        difference.extend(bytes(max(channel, alpha) for channel in delta) + b"\xff")
    write_png(args.output / "reference.png", width, height, reference)
    write_png(args.output / "comparison.png", width * 2, height, rows)
    write_png(args.output / "difference.png", width, height, difference)
    report = {"reference": ref_info, "native": info, "resampled": False,
              "reference_pixels_sha256": hashlib.sha256(reference).hexdigest(),
              "native_pixels_sha256": hashlib.sha256(native).hexdigest(),
              "changed_pixels": changed, "mean_absolute_rgb_error": total / (width * height * 3),
              "changed_alpha_pixels": changed_alpha, "mean_absolute_alpha_error": total_alpha / (width * height),
              "interpretation": "Measurements only; review geometry, states, fonts and service differences separately."}
    (args.output / "comparison.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
