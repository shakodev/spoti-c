"""Generate/check independent Shannon fixtures, or convert checked-in JSON to C.

Only --generate/--check-reference need Cargo/network (first run). CMake uses
--header with Python's standard library and never downloads or runs Rust.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/fixtures/shannon-vectors.json'
REFERENCE = ROOT / 'tools/shannon_reference'
CRATE_HASH = '7ea5b41c9427b56caa7b808cb548a04fb50bb5b9e98590b53f28064ff4174561'


def cases():
    key = bytes(range(32))
    result = [('empty', bytes(32), bytes(4), b'', 4)]
    for size in (1, 2, 3, 4, 5, 7, 8, 9, 31, 32, 33):
        nonce = bytes.fromhex('01020304') if size % 2 else bytes.fromhex('ffffffff')
        result.append((f'raw-{size}', key, nonce,
                       bytes((i * 73 + 0x80) % 256 for i in range(size)), 4))
    result += [('partial-key-nonce-mac', key[:13], bytes.fromhex('80ff010203'),
                bytes(range(19)), 17),
               ('empty-nonce', key, b'', b'abc', 3),
               ('frame-empty', key, bytes(4), bytes.fromhex('040000'), 4),
               ('frame-next', key, bytes.fromhex('00000001'), bytes.fromhex('040000'), 4),
               ('frame-odd', key, bytes.fromhex('10203040'), bytes.fromhex('b2000580ff007f01'), 4),
               ('frame-max', key, bytes.fromhex('10203040'), bytes.fromhex('abffff') +
                bytes((i * 73 + 19) % 256 for i in range(65535)), 4)]
    return result


def reference():
    inputs = cases()
    data = ''.join(f'{name}|{key.hex()}|{nonce.hex()}|{plain.hex()}|{mac_size}\n'
                   for name, key, nonce, plain, mac_size in inputs)
    command = ['cargo', 'run', '--quiet', '--locked', '--manifest-path',
               str(REFERENCE / 'Cargo.toml'), '--target-dir', str(ROOT / '.cache/shannon-reference')]
    result = subprocess.run(command, input=data, text=True, stdout=subprocess.PIPE, check=True)
    lines = result.stdout.splitlines()
    if len(lines) != len(inputs):
        raise RuntimeError('Reference output count mismatch')
    vectors = []
    for (name, key, nonce, plain, mac_size), line in zip(inputs, lines):
        ref_name, cipher, mac = line.split('|')
        if name != ref_name or len(cipher) != len(plain) * 2 or len(mac) != mac_size * 2:
            raise RuntimeError('Reference output shape mismatch')
        vectors.append(dict(name=name, key=key.hex(), nonce=nonce.hex(), plaintext=plain.hex(),
                            ciphertext=cipher, mac=mac))
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    return dict(schema=1, provenance=dict(
        implementation='shannon', version='0.2.0',
        source='https://static.crates.io/crates/shannon/shannon-0.2.0.crate',
        crate_sha256=CRATE_HASH, dependency='byteorder 1.5.0',
        cargo_lock_sha256=sha(REFERENCE / 'Cargo.lock'),
        driver_sha256=sha(REFERENCE / 'src/main.rs'),
        invocation='python tools/shannon_vectors.py --generate',
        note='Unmodified reference; contiguous encrypt/decrypt only. C fragmentation is compared to these results.'),
        vectors=vectors)


def header(output):
    fixture = json.loads(FIXTURE.read_text())
    if fixture['schema'] != 1 or fixture['provenance']['crate_sha256'] != CRATE_HASH:
        raise RuntimeError('Unknown fixture provenance')
    for name, path in (('cargo_lock_sha256', REFERENCE / 'Cargo.lock'),
                       ('driver_sha256', REFERENCE / 'src/main.rs')):
        if hashlib.sha256(path.read_bytes()).hexdigest() != fixture['provenance'][name]:
            raise RuntimeError('Reference provenance changed; regenerate the fixtures')
    vendor = json.loads((ROOT / 'vendor/manifest.json').read_text())
    for item in next(entry for entry in vendor if entry['name'] == 'shannon')['files']:
        if hashlib.sha256((ROOT / item['path']).read_bytes()).hexdigest() != item['sha256']:
            raise RuntimeError(f'Shannon provenance mismatch: {item["path"]}')
    lines = ['/* Generated from tests/fixtures/shannon-vectors.json; do not edit. */']
    records = []
    for i, vector in enumerate(fixture['vectors']):
        decoded = {field: bytes.fromhex(vector[field])
                   for field in ('key', 'nonce', 'plaintext', 'ciphertext', 'mac')}
        if not (1 <= len(decoded['key']) <= 32 and len(decoded['nonce']) <= 16 and
                1 <= len(decoded['mac']) <= 32 and len(decoded['plaintext']) <= 65538 and
                len(decoded['plaintext']) == len(decoded['ciphertext'])):
            raise RuntimeError('Fixture exceeds codec bounds')
        for field, data in decoded.items():
            lines.append(f'static const uint8_t sv_{i}_{field}[] = {{')
            for start in range(0, len(data), 16):
                lines.append('    ' + ','.join(f'0x{byte:02x}' for byte in data[start:start+16]) + ',')
            if not data:
                lines.append('    0,')
            lines.append('};')
        records.append('    {' + json.dumps(vector['name']) +
            f",sv_{i}_key,{len(decoded['key'])},sv_{i}_nonce,{len(decoded['nonce'])}," +
            f"sv_{i}_plaintext,sv_{i}_ciphertext,{len(decoded['plaintext'])},sv_{i}_mac,{len(decoded['mac'])}" + '},')
    lines += ['static const ShannonVector shannon_vectors[] = {', *records, '};']
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(('\n'.join(lines) + '\n').encode())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--generate', action='store_true')
    mode.add_argument('--check-reference', action='store_true')
    mode.add_argument('--header', type=Path)
    args = parser.parse_args()
    if args.header:
        header(args.header)
    else:
        text = json.dumps(reference(), indent=2) + '\n'
        if args.generate:
            FIXTURE.parent.mkdir(parents=True, exist_ok=True)
            FIXTURE.write_bytes(text.encode())
        elif FIXTURE.read_bytes() != text.encode():
            raise RuntimeError('Checked-in vectors differ from pinned reference')
        print(f'{len(cases())} Shannon vectors match the pinned independent reference')
