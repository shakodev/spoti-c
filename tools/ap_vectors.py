"""Offline independent protobuf framing and HMAC fixtures for AP integration.

Uses the existing public signed AP capture and deterministic DH oracle from
crypto-vectors.json. No network, private production keys or implementation calls.
"""
import argparse
import hashlib
import hmac
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def varint(value):
    out = bytearray()
    while value >= 128:
        out.append((value & 127) | 128)
        value >>= 7
    return bytes(out) + bytes([value])


def integer(tag, value):
    return varint(tag << 3) + varint(value)


def blob(tag, value):
    return varint((tag << 3) | 2) + varint(len(value)) + value


def header(path):
    fixtures = json.loads((ROOT / 'tests/fixtures/crypto-vectors.json').read_text())
    dh = fixtures['dh'][-1]
    nonce = bytes(range(16))
    # Both repeated enum lists use the legal protobuf packed representation.
    build = integer(10, 0) + blob(20, varint(0)) + integer(30, 39) + integer(40, 123456789)
    hello = (blob(10, build) + blob(30, varint(0)) +
             blob(50, blob(10, blob(10, bytes.fromhex(dh['public'])) + integer(20, 1))) +
             blob(60, nonce) + blob(70, b'\x1e'))
    wire = b'\0\4' + (len(hello) + 6).to_bytes(4, 'big') + hello
    response = bytes.fromhex(fixtures['ap']['response'])
    transcript = wire + response
    secret = bytes.fromhex(dh['secret'])
    material = b''.join(hmac.digest(secret, transcript + bytes([i]), 'sha1') for i in range(1, 6))
    challenge = hmac.digest(material[:20], transcript, 'sha1')
    reply = blob(10, blob(10, blob(10, challenge))) + blob(20, b'') + blob(30, b'')
    welcome = blob(10, b'fixture') + integer(20, 0) + integer(25, 0) + integer(30, 1) + blob(40, b'fixture-data')
    # The Login fixture is independently encoded from authentication.proto.
    login = blob(10, integer(20, 3) + blob(30, b'fixture-token')) + blob(50, integer(10, 2) + integer(60, 1))
    values = dict(private=bytes.fromhex(dh['private']), public=bytes.fromhex(dh['public']),
                  nonce=nonce, hello=wire, response=response, transcript=transcript,
                  challenge=challenge, tx=material[20:52], rx=material[52:84],
                  reply=(len(reply) + 4).to_bytes(4, 'big') + reply, welcome=welcome, login=login)
    room = 65536 - len(response)
    for payload in range(room - 1, room - 9, -1):
        extra = blob(127, b'Z' * payload)
        if len(extra) == room:
            break
    else:
        raise RuntimeError('Cannot construct exact 64 KiB unknown-field fixture')
    large = (65536).to_bytes(4, 'big') + response[4:] + extra
    assert len(large) == 65536
    large_transcript = wire + large
    large_material = b''.join(hmac.digest(secret, large_transcript + bytes([i]), 'sha1') for i in range(1, 6))
    values.update(large_extra_prefix=extra[:len(extra) - payload],
                  large_challenge=hmac.digest(large_material[:20], large_transcript, 'sha1'),
                  large_tx=large_material[20:52], large_rx=large_material[52:84])
    lines = ['/* Independent Python varint/hashlib/hmac AP fixtures. */']
    for name, value in values.items():
        lines.append(f'static const uint8_t av_{name}[] = {{')
        lines.extend('    ' + ','.join(f'0x{x:02x}' for x in value[i:i + 16]) + ','
                     for i in range(0, len(value), 16))
        lines.append('};')
    lines.append('/* transcript sha256: ' + hashlib.sha256(transcript).hexdigest() + ' */')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(('\n'.join(lines) + '\n').encode())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--header', type=Path, required=True)
    header(parser.parse_args().header)
