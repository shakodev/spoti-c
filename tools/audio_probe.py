"""Offline Release CPU/RAM/diagnostic sample; never opens an account or device."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path, default=Path("build-rel"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build.resolve()
    cache_config = (build / "CMakeCache.txt").read_text()
    if "CMAKE_BUILD_TYPE:STRING=Release" not in cache_config or "ENABLE_ASAN:BOOL=OFF" not in cache_config:
        parser.error("requires a Release build with ENABLE_ASAN=OFF")
    exe = build / "spotic-headless.exe"
    cache_root = (root / ".cache").resolve()
    if not cache_root.is_relative_to(root.resolve()):
        parser.error("fixture cache must resolve inside the workspace")
    summary = {"schema": 1, "device": "fake 48000 Hz stereo float32",
               "windows": str(sys.getwindowsversion()), "processors": os.cpu_count(),
               "executable_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
               "executable_bytes": exe.stat().st_size, "live_acceptance": "pending",
               "scope": "8-second intervals; no GUI, WASAPI or release-gate claim", "runs": []}
    for enabled in (False, True):
        raw = build / ("audio-probe-on.jsonl" if enabled else "audio-probe-off.jsonl")
        errors = raw.with_suffix(".stderr.txt")
        # Each run owns a fresh, bounded fixture cache, outside checked-in data.
        with tempfile.TemporaryDirectory(prefix="audio-probe-", dir=cache_root) as cache:
            if Path(cache).resolve().parent != cache_root:
                raise RuntimeError("unexpected fixture cache location")
            with raw.open("w", encoding="utf-8") as output, errors.open("w", encoding="utf-8") as error:
                command = [str(exe), "--fixture", str(root / "tests/fixtures/audio/synthetic.enc"), "--cache", cache]
                if enabled:
                    command.append("--diagnostics")
                process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=output,
                                           stderr=error, text=True, cwd=root,
                                           creationflags=subprocess.CREATE_NO_WINDOW)
                def send(command):
                    process.stdin.write(command + "\n")
                    process.stdin.flush()
                try:
                    time.sleep(1)  # Excluded startup; the fixture is 12 seconds long.
                    send("metrics")
                    time.sleep(8)
                    send("metrics")
                    send("pause")
                    time.sleep(1)  # Excluded pause/reposition work.
                    send("metrics")
                    time.sleep(8)
                    send("metrics")
                    send("quit")
                    process.stdin.close()
                    if process.wait(timeout=30):
                        raise RuntimeError(f"probe failed; see {errors}")
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
            samples = [json.loads(line) for line in raw.read_text().splitlines()]
            if len(samples) != 5 or not samples[-1]["balanced"]:
                raise RuntimeError(f"incomplete/unbalanced probe: {raw}")
            if any(("audio_diagnostics" in sample) != enabled for sample in samples):
                raise RuntimeError("diagnostic opt-in mismatch")
            run = {"diagnostics": enabled, "cold_cache": True, "raw": raw.name,
                   "peak_working_set": max(s["peak_working_set"] for s in samples), "intervals": []}
            for label, first, last in (("playing", samples[0], samples[1]), ("paused", samples[2], samples[3])):
                seconds = (last["sample_qpc"] - first["sample_qpc"]) / last["qpc_frequency"]
                core = 100 * (last["process_cpu_100ns"] - first["process_cpu_100ns"]) / (seconds * 10_000_000)
                frames = last["frames"] - first["frames"]
                if (label == "playing" and frames < 48000 * 7) or (label == "paused" and frames != 0):
                    raise RuntimeError(f"unexpected playback state: {label}, {frames} frames")
                run["intervals"].append({"state": label, "seconds": seconds, "frames": frames,
                                         "core_percent": core, "machine_percent": core / os.cpu_count(),
                                         "working_set": last["working_set"]})
            if enabled:
                run["diagnostics_final"] = samples[-1]["audio_diagnostics"]
            summary["runs"].append(run)
    target = build / "audio-probe-summary.json"
    target.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"summary": str(target), "runs": [
        {k: v for k, v in run.items() if k != "diagnostics_final"} for run in summary["runs"]]}, indent=2))


if __name__ == "__main__":
    main()
