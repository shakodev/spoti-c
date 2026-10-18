"""Build bounded, analyst-derived PE inspection containers; never execute them.

Every mapped section byte is copied unchanged from an explicitly selected DLL.
Only PE headers are synthesized. Absent code/data/dependencies remain unknown.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED = "ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde"
PAGES = [0x1305000, 0x1306000, 0x1340000, 0x1341000, 0x156F000, 0x1570000,
         0x4D000, 0x18D4000, 0x1C0E000, 0x1C14000, 0x1C5B000, 0x1B3C000,
         0x1C32000, 0x198D000, 0x265F000]


def extract(source, output):
    data = source.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED:
        raise RuntimeError("This extraction map requires the pinned Spotify DLL")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe + 6)[0]
    optional = pe + 24
    base = struct.unpack_from("<Q", data, optional + 24)[0]
    section_at = optional + struct.unpack_from("<H", data, pe + 20)[0]
    original = []
    for index in range(count):
        at = section_at + 40 * index
        virtual_size, rva, size, offset = struct.unpack_from("<IIII", data, at + 8)
        flags = struct.unpack_from("<I", data, at + 36)[0]
        original.append((rva, size, offset, flags, virtual_size))
    header_bytes = 0x400
    optional = 0x80 + 24
    header = bytearray(header_bytes)
    header[:2] = b"MZ"
    struct.pack_into("<I", header, 0x3C, 0x80)
    header[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHIIIHH", header, 0x84, 0x8664, len(PAGES), 0, 0, 0, 240, 0x2022)
    struct.pack_into("<H", header, optional, 0x20B)
    struct.pack_into("<Q", header, optional + 24, base)
    struct.pack_into("<II", header, optional + 32, 0x1000, 0x200)
    struct.pack_into("<HH", header, optional + 40, 6, 0)
    struct.pack_into("<HH", header, optional + 48, 6, 0)
    struct.pack_into("<II", header, optional + 56, max(PAGES) + 0x1000, header_bytes)
    struct.pack_into("<H", header, optional + 68, 3)
    struct.pack_into("<QQQQI", header, optional + 72, 0x100000, 0x1000, 0x100000, 0x1000, 0)
    struct.pack_into("<I", header, optional + 108, 16)
    body = bytearray()
    ranges = []
    for index, rva in enumerate(sorted(PAGES)):
        section = next(item for item in original if item[0] <= rva and rva + 0x1000 <= item[0] + item[1])
        offset = section[2] + rva - section[0]
        payload = data[offset:offset + 0x1000]
        flags = 0x60000020 if section[3] & 0x20000000 else 0xC0000040
        raw_offset = header_bytes + len(body)
        at = optional + 240 + index * 40
        name = f"part{index}".encode().ljust(8, b"\0")
        header[at:at + 8] = name
        struct.pack_into("<IIIIIIHHI", header, at + 8, 0x1000, rva, 0x1000, raw_offset, 0, 0, 0, 0, flags)
        body.extend(payload)
        ranges.append({"source_offset": offset, "rva": hex(rva), "bytes": 0x1000,
                       "container_offset": raw_offset, "sha256": hashlib.sha256(payload).hexdigest()})
    output.mkdir(parents=True, exist_ok=True)
    artifacts = []
    for name, entry in (("clienttoken", 0x1305DCC), ("login5", 0x1340DA8)):
        struct.pack_into("<I", header, optional + 16, entry)
        path = output / f"{name}-inspection.dll"
        payload = header + body
        path.write_bytes(payload)
        artifacts.append({"file": path.name, "entry": hex(base + entry),
                          "sha256": hashlib.sha256(payload).hexdigest()})
    manifest = {"classification": "analyst-derived inspection containers, not shipped binaries",
                "source_sha256": digest, "image_base": hex(base), "ranges": ranges,
                "artifacts": artifacts,
                "limitations": ["Synthesized PE headers; no imports, exports, exceptions or relocations",
                                "Only selected original pages are mapped; missing callees/data remain unknown",
                                "Static disassembly only; never execute these containers"]}
    (output / "extraction.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(artifacts))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    extract(args.source, args.output)
