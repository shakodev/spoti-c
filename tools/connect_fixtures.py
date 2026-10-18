"""Account-free Dealer fixtures, independently encoded with Python's stdlib.

No Spotify traffic/credentials are recorded. Protobuf tags come from the pinned
schema closure. Run explicitly to regenerate the checked-in deterministic data.
"""
import base64
import gzip
import json
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1] / 'tests/fixtures/connect'


def varint(n):
    out = bytearray()
    while n > 127:
        out.append((n & 127) | 128)
        n >>= 7
    return bytes(out) + bytes([n])


def number(tag, n):
    return varint(tag * 8) + varint(n)


def blob(tag, data):
    return varint(tag * 8 + 2) + varint(len(data)) + data


def text(tag, data):
    return blob(tag, data.encode())


def encoded(data):
    return base64.b64encode(data).decode()


def save(name, data):
    (ROOT / name).write_bytes(data)


def request(endpoint, mid, **fields):
    return json.dumps(dict(message_id=mid, sent_by_device_id='fixture-controller',
                           command=dict(endpoint=endpoint, **fields)), separators=(',', ':')).encode()


def envelope(data, key='fixture-key', compress=True):
    return json.dumps(dict(type='request', message_ident='hm://connect-state/v1/player/command',
                           key=key, headers={'Transfer-Encoding': 'gzip'} if compress else {},
                           payload={'compressed': encoded(gzip.compress(data, mtime=0) if compress else data)}),
                      separators=(',', ':')).encode()


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    current = text(1, 'spotify:track:0000000000000000000001')
    next_track = text(1, 'spotify:track:0000000000000000000002')
    playback = number(2, 120) + number(4, 1) + blob(5, current)
    transfer = blob(2, playback) + blob(4, blob(1, next_track))
    save('transfer.pb', transfer)
    page = blob(4, current + text(2, 'current')) + blob(4, next_track + text(2, 'next'))
    context = text(1, 'spotify:album:fixture') + blob(5, page)
    session = blob(2, context) + text(3, 'current')
    save('context-transfer.pb', blob(2, playback) + blob(3, session))
    paginated = text(1, 'spotify:album:fixture') + blob(5, page + text(2, 'https://fixture.invalid/next'))
    save('paginated-transfer.pb', blob(2, playback) + blob(3, blob(2, paginated)))
    save('queue-overflow.pb', blob(2, playback) + blob(4, blob(1, next_track) * 64))
    save('playing-queue.pb', blob(2, playback) + blob(4, blob(1, next_track) + blob(1, current) + number(2, 1)))
    transfer_json = request('transfer', 20, data=encoded(transfer), options={})
    save('transfer.json', transfer_json)
    save('transfer.dealer.json', envelope(transfer_json))
    save('play.json', request('play', 21, context={'uri': 'spotify:track:0000000000000000000001'}, options={}))
    for i, endpoint in enumerate(('resume', 'pause', 'seek_to', 'skip_next', 'skip_prev', 'unknown'), 22):
        data = request(endpoint, i, **({'value': 200} if endpoint == 'seek_to' else {}))
        save(endpoint + '.json', data)
        save(endpoint + '.dealer.json', envelope(data, f'key-{i}'))
    save('cluster.pb', blob(1, number(1, 100) + text(2, 'fixture-device')))
    save('volume.pb', number(1, 32768) + blob(2, number(1, 31)))
    save('logout.pb', blob(1, number(1, 99)))
    save('connection.json', json.dumps(dict(type='message', uri='hm://pusher/v1/connections/fixture',
                                          headers={'Spotify-Connection-Id': 'fixture-connection'}, payloads=[])).encode())
    for kind in ('cluster', 'volume', 'logout'):
        save(kind + '.dealer.json', json.dumps(dict(type='message',
             uri='hm://connect-state/v1/' + ('cluster' if kind == 'cluster' else 'connect/' + kind),
             headers={}, payloads=[encoded((ROOT / (kind + '.pb')).read_bytes())])).encode())
    save('hello.gz', gzip.compress(b'hello dealer', mtime=0))
    simple = gzip.compress(b'hello dealer', mtime=0)
    header = bytearray(simple[:10])
    header[3] = 2 | 4 | 8 | 16
    header.extend(b'\x03\x00abcfixture\x00comment\x00')
    header.extend(struct.pack('<H', zlib.crc32(header) & 65535))
    save('optional.gz', bytes(header) + simple[10:])
    save('stored.gz', gzip.compress(b'hello dealer', compresslevel=0, mtime=0))
    save('expansion.gz', gzip.compress(b'A' * (128 * 1024 + 1), mtime=0))
    save('dynamic.gz', gzip.compress(bytes(range(256)) * 240, mtime=0))


if __name__ == '__main__':
    main()
