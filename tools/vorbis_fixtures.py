"""Synthetic Vorbis corpus and independent libsndfile float32 PCM goldens.

Dependencies are test-only and pinned in vorbis-requirements.txt. Regeneration
is explicit (--write); default verifies committed SHA-256s, sizes and versions.
No Spotify media, credentials or network requests are used.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
import soundfile as sf

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "tests/fixtures/vorbis"


def pages(data):
    result, offset = [], 0
    while offset < len(data):
        assert data[offset:offset + 4] == b"OggS"
        count = data[offset + 26]
        size = 27 + count + sum(data[offset + 27:offset + 27 + count])
        result.append({"offset": offset, "bytes": size,
                       "granule": int.from_bytes(data[offset + 6:offset + 14], "little"),
                       "flags": data[offset + 5]})
        offset += size
    assert offset == len(data)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--write", action="store_true")
    args = parser.parse_args()
    if args.write:
        DEST.mkdir(parents=True, exist_ok=True)
        manifest = {"schema": 1, "oracle": "libsndfile / libvorbis, independent of stb",
                    "soundfile": sf.__version__, "libsndfile": sf.__libsndfile_version__,
                    "numpy": np.__version__, "max_absolute_error": 0.00003, "files": []}
        for name, rate, channels, frames, quality in (
            ("mono22", 22050, 1, 27317, 0.4),
            ("stereo44", 44100, 2, 141127, 0.6),
            ("stereo48", 48000, 2, 432137, 0.9),
        ):
            t = np.arange(frames, dtype=np.float64) / rate
            rng = np.random.default_rng(16)
            left = 0.27 * np.sin(2*np.pi*(300*t + 90*t*t)) + 0.07*rng.uniform(-1, 1, frames)
            right = 0.19*np.sin(2*np.pi*731*t) + 0.11*np.sin(2*np.pi*143*t)
            signal = left[:, None] if channels == 1 else np.column_stack((left, right))
            path = DEST / (name + ".ogg")
            sf.write(path, signal, rate, format="OGG", subtype="VORBIS", compression_level=quality)
            decoded, got_rate = sf.read(path, dtype="float32", always_2d=True)
            assert got_rate == rate and decoded.shape == (frames, channels)
            pcm = DEST / (name + ".f32")
            pcm.write_bytes(decoded.astype("<f4").tobytes())
            row = {"name": name, "sample_rate": rate, "channels": channels, "frames": frames,
                   "pages": pages(path.read_bytes())}
            for suffix in ("ogg", "f32"):
                data = (DEST / (name + "." + suffix)).read_bytes()
                row[suffix] = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
            manifest["files"].append(row)
        manifest["conversions"] = []
        for source, rate, channels in (("mono22", 48000, 2), ("stereo44", 32000, 1)):
            pcm, native_rate = sf.read(DEST / (source + ".ogg"), dtype="float32", always_2d=True)
            count = (len(pcm) * rate + native_rate - 1) // native_rate
            positions = np.arange(count, dtype=np.float64) * native_rate / rate
            converted = np.column_stack([np.interp(positions, np.arange(len(pcm)), pcm[:, channel])
                                         for channel in range(pcm.shape[1])]).astype("<f4")
            if channels == 1:
                converted = converted.mean(axis=1, keepdims=True)
            elif converted.shape[1] == 1:
                converted = np.repeat(converted, 2, axis=1)
            filename = f"{source}-to-{rate}-{channels}.f32"
            data = converted.astype("<f4").tobytes()
            (DEST / filename).write_bytes(data)
            manifest["conversions"].append({"source": source, "sample_rate": rate, "channels": channels,
                "frames": count, "file": filename, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
        (DEST / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    manifest = json.loads((DEST / "manifest.json").read_text(encoding="utf-8"))
    for row in manifest["files"]:
        for suffix in ("ogg", "f32"):
            data = (DEST / (row["name"] + "." + suffix)).read_bytes()
            assert len(data) == row[suffix]["bytes"]
            assert hashlib.sha256(data).hexdigest() == row[suffix]["sha256"]
        golden, rate = sf.read(DEST / (row["name"] + ".ogg"), dtype="float32", always_2d=True)
        assert rate == row["sample_rate"]
        assert golden.astype("<f4").tobytes() == (DEST / (row["name"] + ".f32")).read_bytes()
    for row in manifest.get("conversions", []):
        data = (DEST / row["file"]).read_bytes()
        assert len(data) == row["bytes"] and hashlib.sha256(data).hexdigest() == row["sha256"]
    print("Independent Vorbis/PCM fixture corpus verified")


if __name__ == "__main__":
    main()
