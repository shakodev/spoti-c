"""Verify bounded REA byte records against the pinned, unmodified Spotify DLL.

Data-only inspection containers are analyst-derived, never executed. This tool
only reads supplied REA records and emits a portable manifest; it does not run
Spotify, acquire traffic, or elevate static literals into runtime observations.
"""
import argparse
import hashlib
import json
import struct
from pathlib import Path

EXPECTED = 'ae772df4dca26c53c73ea720836db77584dc431d14d3ab3b5c4b49b0bd2b8fde'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main(source, records, extraction, output):
    data = source.read_bytes()
    if sha(data) != EXPECTED:
        raise ValueError('This manifest requires the selected pinned Spotify DLL')
    pe = struct.unpack_from('<I', data, 60)[0]
    optional = pe + 24
    base = struct.unpack_from('<Q', data, optional + 24)[0]
    sections = optional + struct.unpack_from('<H', data, pe + 20)[0]
    mapping = [struct.unpack_from('<IIII', data, sections + 40 * i + 8)[1:]
               for i in range(struct.unpack_from('<H', data, pe + 6)[0])]
    derived = json.loads(extraction.read_text())
    if derived['source_sha256'] != EXPECTED:
        raise ValueError('Inspection container source identity differs')
    for item in derived['ranges']:
        original = data[item['source_offset']:item['source_offset'] + item['bytes']]
        if sha(original) != item['sha256']:
            raise ValueError('Inspection page differs from original DLL')
    evidence = []
    for name in ('endpoint-ok', 'header-ok', 'http-header-ok', 'command-ok', 'cluster-ok'):
        record = json.loads((records / (name + '.json')).read_text())
        if record.get('isError'):
            raise ValueError('Cannot use failed REA records')
        ev = record.get('structuredContent') or json.loads(record['content'][0]['text'])
        result = ev['normalized_result']
        address = int(result['address'], 16)
        raw = bytes.fromhex(result['bytes_hex'])
        rva = address - base
        section = next((r, n, o) for r, n, o in mapping if r <= rva < r + n)
        offset = section[2] + rva - section[0]
        if not result['complete'] or result['requested_bytes'] != len(raw) or data[offset:offset + len(raw)] != raw:
            raise ValueError('REA record differs from exact original bytes')
        evidence.append(dict(evidence_id=ev['evidence_id'], operation=ev['operation'],
                             provider=ev['provider'], profile_digest=ev['analysis_profile']['digest'],
                             inspection_sha256=ev['subject']['digest']['sha256'], address=result['address'],
                             source_offset=offset, bytes=len(raw), sha256=sha(raw), bytes_hex=raw.hex(),
                             original_bytes_verified=True))
    manifest = dict(source=dict(file='Spotify.dll', version='1.3.4.258', sha256=EXPECTED),
                    classification='Static original-byte observations through analyst-derived data-only PE containers',
                    extraction=derived, evidence=evidence,
                    limitations=['No Spotify code executed and no service request observed',
                                 'Synthesized PE headers/entrypoints; selected original pages unchanged',
                                 'REA reads prove exact literal bytes, not request construction or runtime reachability',
                                 'Methods and envelope semantics use the separately pinned librespot reference',
                                 'The XPUI semantic graph was partial; 269 files were outside its selected scope'])
    output.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Verified {len(evidence)} REA records against original DLL bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--records', type=Path, required=True)
    parser.add_argument('--extraction', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    main(args.source, args.records, args.extraction, args.output)
