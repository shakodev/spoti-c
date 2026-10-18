"""Verify recovered module spans against original snapshot bytes; emit identities.

Requires the original pinned snapshot and the inert AST extraction manifests.
Never executes the snapshot or its source.
"""
import argparse
import hashlib
import json
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def main(args):
    original = args.snapshot.read_bytes()
    extraction = json.loads(args.extraction.read_text())
    modules = json.loads(args.modules.read_text())
    if sha(original) != extraction['source_sha256']:
        raise ValueError('Original snapshot identity differs')
    source = original[extraction['source_offset']:extraction['source_end']]
    if sha(source) != extraction['source_span_sha256'] or sha(source.decode('utf-16-le').encode()) != modules['input_sha256']:
        raise ValueError('Source extraction identity differs')
    request_ids = ['32836', '62037', '6854', '69677', '95096', '26662', '2410',
                   '36920', '41231', '2200', '24063', '83314', '44726', '90185']
    native_ids = ['10608', '33872', '90879', '82379']
    selected = []
    for record in modules['modules']:
        if record['id'] not in request_ids + native_ids:
            continue
        start = extraction['source_offset'] + record['start_utf16'] * 2
        end = extraction['source_offset'] + record['end_utf16'] * 2
        raw = original[start:end]
        fragment = raw.decode('utf-16-le').encode('utf-8')
        if sha(fragment) != record['fragment_sha256']:
            raise ValueError('AST span differs from original snapshot')
        derived = b'self.rspackChunk.push([[0],{' + fragment + b'}]);\n'
        if sha(derived) != record['derived_sha256']:
            raise ValueError('Inert analysis wrapper identity differs')
        selected.append(dict(module=record['id'], source_offset=start, source_end=end,
            source_bytes=len(raw), source_span_sha256=sha(raw),
            utf8_fragment_sha256=sha(fragment), analysis_wrapper_sha256=sha(derived),
            evidence_id='ev_d9373a110e6d8f723a4c2ccf46fc3431ba041c63e3bad0c1e3b91818566661ca'
            if record['id'] in native_ids else 'ev_1598043638d84cda7402b5b188cf254b6ab9c1a85e83d6a54351050338a8f2e3'))
    if len(selected) != len(request_ids + native_ids):
        raise ValueError('Required module span missing')
    result = dict(extraction=extraction, parser='@babel/parser 8.0.4',
        transformation=modules['transformation'], selected_modules=selected,
        authority='Verified original bytes and analyst-derived inert wrappers; no runtime equivalence claim')
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(f'Verified {len(selected)} exact original module spans')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', type=Path, required=True)
    parser.add_argument('--extraction', type=Path, required=True)
    parser.add_argument('--modules', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    main(parser.parse_args())
