"""Merge locally sanitized native RPC observations into the HTTP fixture manifest.

This is an offline publisher, not a capture tool. The Evidence ID identifies
static source analysis; it must not be represented as a REA live-capture ID.
"""
import argparse
import hashlib
import json
from pathlib import Path

SOURCE_SHA = 'aefd55fa7f292f438ee7cec4ed4803a2b7ff6b7dc6a8453ec0e32b151d2b07df'
SOURCE_EV = 'ev_588525847e8c10120ab396ab3a47029259f9e9a4487c761eb56ea3f5040a677e'
CLIENT_VERSION = '1.3.5.3058'
CAP = 262144
BINDINGS = {
    2: ('library', 'spotify.your_library_esperanto.proto.YourLibraryService', 'All'),
    3: ('playlist', 'spotify.playlist_esperanto.proto.PlaylistDataService', 'Get'),
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def status(record, operation):
    response = record.get('response', {})
    return response.get('statusCode') if operation == 2 else response.get('status', {}).get('statusCode')


def collection(record, operation):
    response = record.get('response', {})
    return response.get('entity') if operation == 2 else response.get('data', {}).get('item')


def select(records, operation):
    _, service, method = BINDINGS[operation]
    candidates = [record for record in records if record.get('kind') == 'native-rpc'
                  and record.get('service') == service and record.get('method') == method
                  and record.get('stream') is False and 'response' in record]
    chosen = {}
    for record in candidates:
        request = record.get('request', {})
        code, items = status(record, operation), collection(record, operation)
        if operation == 2:
            header = request.get('header', {})
            offset, length = header.get('skip'), header.get('length')
            normal_sort = header.get('sortOrder', {}).get('sortOrder') == 0
            if code == 404:
                chosen.setdefault('error', record)
            elif code == 200 and isinstance(items, list):
                if normal_sort and length == 2 and len(items) == 2 and offset in (0, 2):
                    chosen.setdefault('positive' if offset == 0 else 'pagination', record)
                elif offset == 999999 and not items:
                    chosen.setdefault('exhaustion', record)
        else:
            page = request.get('query', {}).get('range', {})
            offset = page.get('start')
            if code == 400 and request.get('uri'):
                chosen.setdefault('error', record)
            elif code == 200 and isinstance(items, list) and len(items) == 2 and offset in (0, 2):
                chosen.setdefault('positive' if offset == 0 else 'pagination', record)
    required = {'positive', 'pagination', 'error'} | ({'exhaustion'} if operation == 2 else set())
    if set(chosen) != required:
        raise ValueError(f'{BINDINGS[operation][0]} lacks required native cases: {sorted(required - set(chosen))}')
    return chosen


def publish(args):
    capture_bytes = args.capture.read_bytes()
    capture = json.loads(capture_bytes)
    if capture.get('restored') is not True or capture.get('dropped') != 0:
        raise ValueError('Native capture must confirm restoration and no dropped records')
    manifest_path = args.output / 'manifest.json'
    manifest = read(manifest_path)
    if manifest.get('schema_version') != 1:
        raise ValueError('Unsupported fixture manifest')
    # Replace only this publisher's records; preserve HTTP and older RPC evidence.
    fixtures = [entry for entry in manifest['fixtures']
                if not entry['file'].startswith(('native-library-', 'native-playlist-'))]
    parent_sha = sha(capture_bytes)
    for operation, (name, service, method) in BINDINGS.items():
        cases = select(capture['records'], operation)
        for label, record in sorted(cases.items()):
            kind = 'observed-alternative' if label == 'exhaustion' else 'observed-' + label
            filename = f'native-{name}-observed-{label}.json'
            data = (json.dumps(record, ensure_ascii=True, separators=(',', ':')) + '\n').encode()
            if len(data) > CAP:
                raise ValueError('Native projection exceeds the bounded fixture allowance')
            items = collection(record, operation)
            pointer = ('/response/entity' if operation == 2 else '/response/data/item') if isinstance(items, list) else ''
            (args.output / filename).write_bytes(data)
            fixtures.append(dict(file=filename, bytes=len(data), sha256=sha(data),
                count_pointer=pointer, expected_count=len(items) if isinstance(items, list) else 0,
                kind=kind, operation=operation, wire_operation=method,
                provenance='Locally sanitized decoded native RPC projection; not an HTTP request or REA live capture',
                parent_sha256=parent_sha, evidence_id=SOURCE_EV, source_evidence_id=SOURCE_EV,
                captured_client_version=CLIENT_VERSION))
        metadata = next(entry for entry in manifest['operations'] if entry['id'] == operation)
        metadata.update(wire_operation=method, rpc_service=service, enabled=True,
                        missing_proofs=0, source_sha256=SOURCE_SHA, evidence_id=SOURCE_EV)
    manifest['fixtures'] = sorted(fixtures, key=lambda entry: (entry['operation'], entry['file']))
    manifest['native_runtime_captures'] = [dict(file=args.capture.name, sha256=parent_sha,
        captured_client_version=CLIENT_VERSION, authority='locally sanitized decoded runtime observation',
        source_evidence_id=SOURCE_EV, source_evidence_scope='static request/response source analysis')]
    manifest['release_ready'] = all(entry['enabled'] and entry['missing_proofs'] == 0
                                    for entry in manifest['operations'])
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Published {sum(entry["file"].startswith("native-") for entry in fixtures)} native RPC fixtures; '
          f'{len(fixtures)} total; release_ready={manifest["release_ready"]}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('tests/fixtures/api'))
    publish(parser.parse_args())
