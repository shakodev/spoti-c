"""Publish supporting, already sanitized native HTTP protobuf projections.

These fixtures document backing services, never substitute for the primary
Library/Playlist RPC fixtures, and contribute no primary-operation proof bits.
"""
import argparse
import hashlib
import json
from pathlib import Path


def sha(data):
    return hashlib.sha256(data).hexdigest()


def publish(capture, evidence, schemas, output):
    raw = capture.read_bytes()
    ev = json.loads(evidence.read_text(encoding='utf-8'))['normalized_result']
    if ev['artifact']['sha256'] != sha(raw):
        raise ValueError('Capture identity differs from REA observation')
    schema = json.loads(schemas.read_text(encoding='utf-8'))
    decoder = schema['dependency_closure']['file_descriptor_set_sha256']
    manifest_path = output / 'manifest.json'
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    manifest['fixtures'] = [f for f in manifest['fixtures'] if f['kind'] != 'observed-backend-http']
    selected = {}
    for entry in json.loads(raw)['log']['entries']:
        meta = entry['_sanitization']
        if (meta['authority'] != 'locally-redacted native proxy observation' or
                meta['decoder_descriptor_set_sha256'] != decoder):
            raise ValueError('Unknown sanitizer or historical decoder')
        req, resp = entry['request'], entry['response']
        path, case = req['url'].split('.com', 1)[1], meta['source_case']
        if case == 'official-client':
            # Retain one representative for each independent backing route/status.
            if '/collection/v2/delta' not in path and not path.endswith('/diff'):
                continue
            case = ('collection-delta' if '/collection/' in path else
                    'rootlist-diff' if '/rootlist/' in path else 'playlist-diff') + '-' + str(resp['status'])
        selected.setdefault(case, entry)
    for case, entry in selected.items():
        meta, req, resp = entry['_sanitization'], entry['request'], entry['response']
        op = 2 if '/collection/' in req['url'] or '/rootlist' in req['url'] else 3
        payload = dict(request=dict(method=req['method'], url=req['url'], headers=req['headers'],
            header_names=meta['header_names'], query=req['queryString'], body=json.loads(req['postData']['text'])),
            response=dict(status=resp['status'], body=json.loads(resp['content']['text'])))
        body = payload['response']['body'].get('decoded', {})
        pointer, count = '', 0
        if isinstance(body.get('items'), list):
            pointer, count = '/response/body/decoded/items', len(body['items'])
        elif isinstance(body.get('contents', {}).get('items'), list):
            pointer, count = '/response/body/decoded/contents/items', len(body['contents']['items'])
        name = 'backend-' + case + '.json'
        data = (json.dumps(payload, ensure_ascii=True, separators=(',', ':'))+'\n').encode()
        if len(data) > 262144:
            raise ValueError('Fixture body exceeds local bound')
        (output/name).write_bytes(data)
        manifest['fixtures'].append(dict(file=name, bytes=len(data), sha256=sha(data),
            count_pointer=pointer, expected_count=count, operation=op, wire_operation=None,
            kind='observed-backend-http', provenance=meta['representation'],
            evidence_id=ev['parent_evidence_id'], parent_sha256=sha(raw),
            original_response_bytes=meta['original_response_bytes'],
            original_response_sha256=meta['original_response_sha256'],
            decoder_descriptor_set_sha256=decoder, captured_client_version='1.3.5.3058',
            source_case=meta['source_case']))
    manifest['native_backend_capture'] = dict(sha256=sha(raw), evidence_id=ev['parent_evidence_id'],
        decoder_descriptor_set_sha256=decoder, source_sha256=schema['source']['sha256'])
    manifest_path.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    print(f'Published {len(selected)} supporting backend fixtures; {len(manifest["fixtures"])} total')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--evidence', type=Path, required=True)
    parser.add_argument('--schemas', type=Path, default=Path('docs/specs/api-contract-native-backends.json'))
    parser.add_argument('--output', type=Path, default=Path('tests/fixtures/api'))
    args = parser.parse_args()
    publish(args.capture, args.evidence, args.schemas, args.output)
