"""Inventory a local Spotify SPA and collect bounded CSS evidence, without execution.

Copyright (c) spoti-c contributors. No Spotify assets are copied into the client.
The optional extraction directory is for local REA analysis, not redistribution.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def collect(archive, output, extract):
    raw = archive.read_bytes()
    files, rules, skipped = [], [], []
    chosen = {
        "xpui-snapshot.css", "dwp-top-bar.css", "dwp-now-playing-bar.css",
        "xpui-routes-your-library-x.css", "xpui-routes-playlist.css",
        "dwp-home-chips-row.css", "xpui-root-dialogs.css",
    }
    interesting = re.compile(
        r"--(background|text|essential|decorative|encore|panel|nav-bar|left-sidebar|"
        r"right-sidebar|now-playing|global-nav)|font-family|font-size|font-weight|"
        r"border-radius|prefers-reduced-motion|focus-visible|scrollbar|@media|@container"
    )
    if extract:
        extract.mkdir(parents=True, exist_ok=False)
    with zipfile.ZipFile(archive) as z:
        for entry in z.infolist():
            if entry.is_dir():
                skipped.append(entry.filename)
                continue
            name = PurePosixPath(entry.filename)
            if name.is_absolute() or ".." in name.parts or "\\" in entry.filename:
                raise ValueError(f"Unsafe archive file: {entry.filename!r}")
            data = z.read(entry)
            files.append({"path": entry.filename, "bytes": len(data), "sha256": digest(data)})
            if extract:
                target = (extract / str(name)).resolve()
                if not target.is_relative_to(extract.resolve()):
                    raise ValueError("Extraction escapes its destination")
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(data)
            if entry.filename not in chosen:
                continue
            css = data.decode("utf-8")
            for match in re.finditer(r"([^{}]+)\{([^{}]*)\}", css):
                selector, body = match.groups()
                if interesting.search(selector + body):
                    rules.append({
                        "file": entry.filename, "offset_utf8": len(css[:match.start()].encode()),
                        "selector": selector, "declarations": body,
                    })
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({
        "archive": str(archive), "sha256": digest(raw),
        "scope": "Static packaged CSS; does not prove active runtime variants or pixels.",
        "skipped_directory_entries": skipped, "files": files, "rules": rules,
    }, indent=2), encoding="utf-8")
    print(json.dumps({"sha256": digest(raw), "files": len(files), "rules": len(rules),
                      "report": str(output), "extracted": str(extract) if extract else None}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--extract", type=Path)
    args = parser.parse_args()
    collect(args.archive.resolve(), args.output.resolve(),
            args.extract.resolve() if args.extract else None)
