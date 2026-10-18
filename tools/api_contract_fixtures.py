"""Publish bounded, already locally redacted API observations and provenance.

Never fetches endpoints or accepts raw authenticated captures. Fault-injection
cases are explicitly separate from observed service replies and cannot enable
a contract. The manifest is checked independently by the C api_contract test.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path

SNAPSHOT = 'c20d9408009a281ee4152b63077a8309eb17ec172bbf80b80dc7879d586bb099'
SOURCE_EV = 'ev_1598043638d84cda7402b5b188cf254b6ab9c1a85e83d6a54351050338a8f2e3'
NATIVE_EV = 'ev_d9373a110e6d8f723a4c2ccf46fc3431ba041c63e3bad0c1e3b91818566661ca'
SEARCH_EV = 'ev_40a8864feaeed9523967983d24c87a0f7b49671e3921eb187ddef6af5b19344d'
SEARCH_SHA = '06ef397f7adcbbf695f2bc08b2aef737ad05325712554f0d75135f0da547b4f0'
CAP = 262144
OPERATIONS = [
    ('home', 'home', 511), ('home_section', 'homeSection', 207),
    ('library', 'YourLibraryService.All', 1), ('playlist', 'PlaylistDataService.Get', 1),
    ('search', 'searchDesktop', 207), ('search_tracks', 'searchTracks', 1535),
    ('album', 'getAlbum', 2047), ('album_tracks', 'queryAlbumTracks', 1535),
    ('artist', 'queryArtistOverview', 511), ('lyrics', None, 1023),
    ('search_list', 'searchTopResultsList', 511),
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def pointer_get(obj, path):
    for key in path.split('/')[1:]:
        obj = obj[key.replace('~1', '/').replace('~0', '~')]
    return obj


def first_array(value, path=''):
    if isinstance(value, list):
        return path
    if isinstance(value, dict):
        for key, child in value.items():
            result = first_array(child, path + '/' + key.replace('~', '~0').replace('/', '~1'))
            if result is not None:
                return result
    return None


def publish(args):
    args.output.mkdir(parents=True, exist_ok=True)
    evidence = {e['normalized_result']['artifact']['sha256']:
                e['normalized_result']['parent_evidence_id'] for e in read(args.evidence)}
    selected, parents = {}, []
    for capture in args.capture:
        raw = capture.read_bytes()
        har = json.loads(raw)
        if sha(raw) not in evidence:
            raise ValueError('Capture lacks an exact REA artifact identity')
        parents.append(dict(file=capture.name, sha256=sha(raw), evidence_id=evidence[sha(raw)]))
        for entry in har['log']['entries']:
            if entry.get('_sanitization', {}).get('authority') != 'locally-redacted observation':
                raise ValueError('Only the local sanitizer output is accepted')
            req = entry['request']
            if any(h['name'].lower() not in {'accept', 'content-type', 'accept-language',
                    'app-platform', 'spotify-app-version'} for h in req['headers']):
                raise ValueError('Unexpected request header in sanitized input')
            body = json.loads(req['postData']['text']) if 'postData' in req else None
            wire = body.get('operationName') if body else None
            op = next((i for i, (_, name, _) in enumerate(OPERATIONS) if name == wire and name), None)
            if '/color-lyrics/v2/' in req['url']:
                op = 9
            if op is None:
                continue
            response = json.loads(entry['response']['content']['text'])
            wrapped = dict(request=dict(method=req['method'], url=req['url'], headers=req['headers'],
                                        header_names=entry['_sanitization']['header_names'],
                                        body=body, query=req['queryString']),
                           response=dict(status=entry['response']['status'], body=response))
            variables = (body or {}).get('variables', {})
            album = (response or {}).get('data', {}).get('albumUnion', {})
            kind = 'observed-positive'
            if entry['response']['status'] >= 400 or (response or {}).get('errors') or (
                    op == 6 and album.get('__typename') != 'Album'):
                kind = 'observed-error'
            elif variables.get('offset', 0) > 0:
                kind = 'observed-pagination'
            selected[op, wire, kind] = dict(payload=wrapped, kind=kind, operation=op,
                wire_operation=wire, provenance='Locally sanitized service observation; strings redacted before disk/REA',
                parent_sha256=sha(raw), original_response_sha256=entry['_sanitization']['original_response_sha256'])
            selected[op, wire, kind]['evidence_id'] = evidence[sha(raw)]
            selected[op, wire, kind]['original_response_bytes'] = entry['_sanitization']['original_response_bytes']
    if args.native:
        native = read(args.native)
        if not native.get('restored'):
            raise ValueError('Native observation did not confirm restoration')
        for op, service in [(2, 'YourLibraryService'), (3, 'PlaylistDataService')]:
            records = [r for r in native['records'] if r['service'].endswith('.' + service)
                       and r['method'] in ('All', 'StreamAll', 'Get', 'Subscribe') and 'response' in r]
            if records:
                selected[op, service, 'observed-native-rpc'] = dict(payload=records[0], kind='observed-native-rpc',
                    operation=op, wire_operation=service + '.' + records[0]['method'],
                    provenance='Locally redacted decoded Esperanto projection; not external HTTP evidence',
                    parent_sha256=sha(args.native.read_bytes()), evidence_id=NATIVE_EV)
    # Controlled mutations exercise partial-error handling, not server behavior.
    for op in (0, 8):
        observed = next((v for (i, _, k), v in selected.items() if i == op and k == 'observed-positive'), None)
        if observed:
            item = copy.deepcopy(observed)
            item['kind'] = 'fault-injection'
            item['payload']['response'] = dict(status=200, body=dict(data=None,
                errors=[dict(message='synthetic fixture error')]))
            item['provenance'] = 'Controlled mutation of sanitized positive; not an observed Spotify error'
            selected[op, item['wire_operation'], 'fault-injection'] = item
    fixtures = []
    for (op, _, _), item in sorted(selected.items(), key=lambda v: (v[0][0], v[0][2])):
        name = OPERATIONS[op][0] + '-' + item['kind'] + '.json'
        data = (json.dumps(item.pop('payload'), ensure_ascii=True, separators=(',', ':')) + '\n').encode()
        if len(data) > CAP:
            raise ValueError('Fixture exceeds bounded response allowance')
        payload = json.loads(data)
        path = first_array(payload.get('response', {}), '/response') or ''
        if op == 2:
            path = '/response/entity'
        if op == 3 and 'data' in payload.get('response', {}):
            path = '/response/data/item'
        if op in (6, 7) and payload.get('response', {}).get('body', {}).get('data', {}).get('albumUnion', {}).get('__typename') == 'Album':
            path = '/response/body/data/albumUnion/tracksV2/items'
        if op == 9 and payload['response']['body'] is not None:
            path = '/response/body/lyrics/lines'
        count = len(pointer_get(payload, path)) if path else 0
        (args.output / name).write_bytes(data)
        fixtures.append(dict(file=name, bytes=len(data), sha256=sha(data), count_pointer=path,
                             expected_count=count, **item))
    operations = []
    for i, (name, wire, proofs) in enumerate(OPERATIONS):
        operations.append(dict(id=i, name=name, wire_operation=wire, enabled=proofs == 2047,
            missing_proofs=2047 & ~proofs, source_sha256=SEARCH_SHA if i in (4, 5, 10) else SNAPSHOT,
            evidence_id=NATIVE_EV if i in (2, 3) else SEARCH_EV if i in (4, 5, 10) else SOURCE_EV))
    manifest = dict(schema_version=1, issue=21, release_ready=False,
                    operations=operations, captures=parents, fixtures=fixtures)
    (args.output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(f'Published {len(fixtures)} bounded sanitized fixtures; required unresolved contracts keep issue open')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--capture', type=Path, action='append', default=[])
    parser.add_argument('--native', type=Path)
    parser.add_argument('--evidence', type=Path, default=Path('docs/specs/api-contract-capture-rea.json'))
    parser.add_argument('--output', type=Path, default=Path('tests/fixtures/api'))
    publish(parser.parse_args())
