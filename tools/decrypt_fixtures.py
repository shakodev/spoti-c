"""Independent synthetic AES-CTR oracle for #15; never reads account/media data.

Run with the existing crypto-requirements environment. --write regenerates;
the default verifies committed fixture bytes and their manifest. No CNG calls.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "tests/fixtures/decrypt"
KEY = bytes(range(16))
IV = bytes.fromhex("72e067fbddcbcf77ebe8bc643f630d93")
BLOCK = 65536


def plaintext(offset, count):
    data = bytearray(((p * 29 + 17) ^ (p >> 8)) & 255
                     for p in range(offset, offset + count))
    prefix = bytearray(172)
    # Opaque prefix deliberately has no invented magic/structure.
    struct.pack_into("<ffff", prefix, 144, -6.0, 0.75, -3.0, 1.25)
    prefix[167:172] = b"OggS\0"
    for p in range(offset, min(offset + count, len(prefix))):
        data[p - offset] = prefix[p]
    return bytes(data)


def encrypt(offset, plain):
    counter = int.from_bytes(IV, "big") + offset // 16
    skip = offset % 16
    cipher = Cipher(algorithms.AES(KEY), modes.CTR(counter.to_bytes(16, "big"))).encryptor()
    return cipher.update(bytes(skip) + plain)[skip:] + cipher.finalize()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    carries = [(1 << bits) - (int.from_bytes(IV, "big") & ((1 << bits) - 1))
               for bits in (8, 16, 32)]
    carries = [n * 16 for n in carries]
    files = {"continuous.bin": encrypt(0, plaintext(0, BLOCK * 2 + 333))}
    # Independent short windows are installed inside encrypted cache blocks by
    # the test. They cross 16-bit and 32-bit counter carry boundaries.
    windows = []
    for bits, carry in zip((16, 32), carries[1:]):
        offset, count = carry - 32, 96
        name = f"carry{bits}.bin"
        files[name] = encrypt(offset, plaintext(offset, count))
        windows.append({"file": name, "offset": offset, "bytes": count})
    manifest = {"schema": 1, "oracle": "Python cryptography AES-128 CTR, big-endian counter",
                "key_hex": KEY.hex(), "iv_hex": IV.hex(), "carry_offsets": carries,
                "windows": windows,
                "sha256": {name: hashlib.sha256(data).hexdigest() for name, data in files.items()}}
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    if args.write:
        DEST.mkdir(parents=True, exist_ok=True)
        for name, data in files.items():
            (DEST / name).write_bytes(data)
    else:
        for name, data in files.items():
            if (DEST / name).read_bytes() != data:
                raise SystemExit(f"Fixture differs: {name}")
    print("Independent decryption fixtures verified")


if __name__ == "__main__":
    main()
