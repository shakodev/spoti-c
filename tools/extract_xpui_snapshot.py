"""Extract inert UTF-16 XPUI source from the pinned installed V8 snapshot.

Never executes snapshot or JavaScript code. Output is an analyst-derived input
for REA, not a recovered runtime observation.
"""
import argparse
import hashlib
import json
from pathlib import Path


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('snapshot', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--expected-sha256', default='c20d9408009a281ee4152b63077a8309eb17ec172bbf80b80dc7879d586bb099',
                        help='Explicitly pinned artifact digest; defaults to Spotify 1.3.4.258')
    args = parser.parse_args()
    data = args.snapshot.read_bytes()
    expected = args.expected_sha256
    if len(expected) != 64 or any(c not in '0123456789abcdef' for c in expected):
        raise ValueError('Expected digest must be 64 lowercase hexadecimal characters')
    if digest(data) != expected:
        raise ValueError('Snapshot identity differs from explicitly pinned digest')
    start_marker = 'var __webpack_modules__='.encode('utf-16-le')
    end_marker = '//# sourceMappingURL=xpui-modules.js.map'.encode('utf-16-le')
    if data.count(start_marker) != 1 or data.count(end_marker) != 1:
        raise ValueError('Source boundaries are not unique')
    start = data.index(start_marker)
    end = data.index(end_marker) + len(end_marker)
    if end <= start or (end - start) % 2:
        raise ValueError('Invalid source span')
    raw = data[start:end]
    source = raw.decode('utf-16-le', errors='strict').encode('utf-8')
    args.output.mkdir(exist_ok=False, parents=True)
    (args.output / 'xpui-modules.js').write_bytes(source)
    manifest = dict(source_file='v8_context_snapshot.bin', source_sha256=expected,
                    source_bytes=len(data), source_offset=start, source_end=end,
                    encoding='utf-16-le', source_span_sha256=digest(raw),
                    output_file='xpui-modules.js', output_bytes=len(source),
                    output_sha256=digest(source),
                    authority='analyst-derived inert source; no code execution')
    (args.output / 'extraction.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    main()
