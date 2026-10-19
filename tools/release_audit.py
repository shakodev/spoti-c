"""Fail-closed source, dependency, artifact and import audit for issue #35."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[1]
    build = args.build.resolve()
    failures = []
    manifest = json.loads((root / "vendor/manifest.json").read_text())
    dependencies = []
    for dependency in manifest:
        files = []
        for entry in dependency["files"]:
            path = root / entry["path"]
            valid = path.is_file() and digest(path) == entry["sha256"]
            if not valid:
                failures.append("vendor identity mismatch: " + entry["path"])
            files.append({"path": entry["path"], "valid": valid,
                          "sha256": entry["sha256"], "source": entry.get("source")})
        dependencies.append({"name": dependency["name"], "files": files,
                             "licenses": [f["path"] for f in files if "license" in f["path"].lower()]})
        if not dependencies[-1]["licenses"]:
            failures.append("missing license: " + dependency["name"])
    commands = json.loads((build / "compile_commands.json").read_text())
    for command in commands:
        if pathlib.Path(command["file"]).suffix.lower() != ".c":
            failures.append("non-C translation unit: " + command["file"])
        flags = command.get("command", " ".join(command.get("arguments", [])))
        if "/W4" not in flags or "/WX" not in flags:
            failures.append("missing strict warnings: " + command["file"])
        if "fsanitize" in flags or "SPOTIC_ASAN" in flags:
            failures.append("instrumented performance binary: " + command["file"])
    artifacts = []
    for name in ("spotic.exe", "spotic-headless.exe", "bench_release.exe",
                 "bench_idle.exe", "bench_navigation.exe", "bench_headless.exe"):
        path = build / name
        if not path.is_file():
            failures.append("missing artifact: " + name)
            continue
        result = subprocess.run(["dumpbin", "/imports", str(path)], capture_output=True, text=True)
        (build / (name + ".imports.txt")).write_text(result.stdout, encoding="utf-8")
        imports = sorted(set(re.findall(r"(?im)^\s*([\w.+-]+\.dll)\s*$", result.stdout)))
        if result.returncode or not imports:
            failures.append("import audit failed: " + name)
        forbidden = [dll for dll in imports if re.search(
            r"msvcp|msvcprt|libstdc\+\+|libc\+\+|d3dcompiler|clang_rt|asan", dll, re.I)]
        if forbidden:
            failures.append(name + " forbidden imports: " + ", ".join(forbidden))
        size = path.stat().st_size
        if name in ("spotic.exe", "spotic-headless.exe") and size >= 2_000_000:
            failures.append(name + " exceeds executable size limit")
        artifacts.append({"name": name, "bytes": size, "sha256": digest(path), "imports": imports})
    config = (root / "src/audio/miniaudio_config.h").read_text()
    for flag in ("MA_ENABLE_ONLY_SPECIFIC_BACKENDS", "MA_ENABLE_WASAPI", "MA_NO_DECODING",
                 "MA_NO_ENCODING", "MA_NO_RESOURCE_MANAGER", "MA_NO_NODE_GRAPH", "MA_NO_ENGINE",
                 "MA_NO_WAV", "MA_NO_FLAC", "MA_NO_MP3"):
        if not re.search(r"^#define " + flag + r"\b", config, re.M):
            failures.append("unused audio backend/codec guard missing: " + flag)
    record = {"schema": 1, "translation_units": len(commands), "dependencies": dependencies,
              "artifacts": artifacts, "failures": failures}
    (build / "release-audit.json").write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
    for failure in failures:
        print("FAIL:", failure)
    print(f"Release audit: {len(commands)} C units, {len(artifacts)} artifacts, {len(failures)} failures")
    return bool(failures)


if __name__ == "__main__":
    raise SystemExit(main())
