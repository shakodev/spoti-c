"""Independent crypto oracle (--generate/--check) and offline C fixture converter.

Oracle uses Python integer pow, hashlib/hmac and cryptography 46.0.3. Normal
CMake --header conversion needs only the standard library and no network.
"""
import argparse
import hashlib
import hmac
import importlib.metadata
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = ROOT / 'tests/fixtures/crypto-vectors.json'


def constants():
    text = (ROOT / 'src/protocol/crypto_constants.c').read_text()
    return [bytes(int(x, 16) for x in re.findall(r'0x([0-9a-f]{2})',
            text.split(name + '[')[1].split('};')[0])) for name in ('crypto_dh_prime', 'crypto_ap_modulus')]


def generate(ap):
    if importlib.metadata.version('cryptography') != '46.0.3':
        raise RuntimeError('Install tools/crypto-requirements.txt for the independent oracle')
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.asymmetric import rsa, padding
    from cryptography.hazmat.primitives import hashes
    prime, modulus = constants()
    p = int.from_bytes(prime, 'big')
    be = lambda x: x.to_bytes(max(1, (x.bit_length() + 7) // 8), 'big')
    rsa.RSAPublicNumbers(65537, int.from_bytes(modulus, 'big')).public_key().verify(
        bytes.fromhex(ap['signature']), bytes.fromhex(ap['gs']), padding.PKCS1v15(), hashes.SHA1())
    dh = []
    a = int.from_bytes(hashlib.sha256(b'spoti-c independent DH exponent a').digest(), 'big')
    for b in range(2, 4096):
        shared = pow(pow(2, b, p), a, p)
        if len(be(shared)) == 95:
            break
    else:
        raise RuntimeError('No leading-zero DH vector')
    for name, private, remote in [('small-public-and-secret', 2, 8),
                                  ('leading-zero-secret', a, pow(2, b, p)),
                                  ('signed-ap-secret', a, int(ap['gs'], 16))]:
        dh.append(dict(name=name, private=private.to_bytes(96, 'big').hex(),
                       public=be(pow(2, private, p)).hex(), peer=be(remote).hex(),
                       secret=be(pow(remote, private, p)).hex()))
    hashes_out = []
    for name, data in [('empty', b''), ('abc', b'abc'),
                       ('multi-block', b'abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq'),
                       ('binary', bytes(range(256)) * 4)]:
        hashes_out.append(dict(name=name, data=data.hex(), sha1=hashlib.sha1(data).hexdigest(),
                               sha256=hashlib.sha256(data).hexdigest()))
    macs = []
    for name, key, data in [('rfc2202-1', b'\x0b' * 20, b'Hi There'),
                            ('rfc2202-2', b'Jefe', b'what do ya want for nothing?'),
                            ('rfc2202-6', b'\xaa' * 80, b'Test Using Larger Than Block-Size Key - Hash Key First'),
                            ('empty', b'', b'')]:
        macs.append(dict(name=name, key=key.hex(), data=data.hex(),
                         digest=hmac.digest(key, data, 'sha1').hex()))
    transcripts = []
    wire = bytes.fromhex(ap['request'] + ap['response'])
    for vector in dh[1:]:
        secret = bytes.fromhex(vector['secret'])
        material = b''.join(hmac.digest(secret, wire + bytes([i]), 'sha1') for i in range(1, 6))
        transcripts.append(dict(name=vector['name'], secret=secret.hex(), transcript=wire.hex(),
            material=material.hex(), challenge=hmac.digest(material[:20], wire, 'sha1').hex(),
            tx=material[20:52].hex(), rx=material[52:84].hex()))
    ctr = []
    def aes(name, key, iv, offset, plain):
        counter = int.from_bytes(iv, 'big') + offset // 16
        cipher = Cipher(algorithms.AES(key), modes.CTR(counter.to_bytes(16, 'big'))).encryptor()
        skip = offset % 16
        result = cipher.update(bytes(skip) + plain)[skip:] + cipher.finalize()
        ctr.append(dict(name=name, key=key.hex(), iv=iv.hex(), offset=offset,
                        plaintext=plain.hex(), ciphertext=result.hex()))
    key = bytes.fromhex('2b7e151628aed2a6abf7158809cf4f3c')
    iv = bytes.fromhex('f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff')
    nist = bytes.fromhex('6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51'
                         '30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710')
    aes('nist-sp800-38a-f5.1', key, iv, 0, nist)
    assert ctr[-1]['ciphertext'] == ('874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff'
                                    '5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee')
    audio_iv = bytes.fromhex('72e067fbddcbcf77ebe8bc643f630d93')
    plain = bytes((i * 37 + 128) % 256 for i in range(2071))
    for offset in (0, 1, 15, 16, 17, 166, 167):
        aes(f'audio-offset-{offset}', key, audio_iv, offset, plain)
    aes('carry-64', key, bytes.fromhex('0000000000000000ffffffffffffffff'), 15, plain[:80])
    aes('last-counter-byte', key, b'\xff' * 16, 15, b'x')
    aes('last-file-byte', key, bytes(16), 2**64 - 1, b'x')
    return dict(schema=1, provenance=dict(oracle='Python pow/hashlib/hmac; cryptography 46.0.3',
        reference_revision='19959512c212cbc48a3ff64382fe204f52c659a7',
        note='AP fixture is a public unauthenticated hello only; no credentials or login.'),
        prime=prime.hex(), modulus=modulus.hex(), ap=ap, dh=dh, hashes=hashes_out,
        hmac=macs, transcripts=transcripts, ctr=ctr)


def header(path):
    value = json.loads(FIXTURE.read_text())
    if value['schema'] != 1:
        raise RuntimeError('Unknown fixture schema')
    lines = ['/* Generated from crypto-vectors.json; no private production keys. */']
    def array(name, text):
        data = bytes.fromhex(text)
        lines.append(f'static const uint8_t cv_{name}[] = {{')
        lines.extend('    ' + ','.join(f'0x{x:02x}' for x in data[i:i+16]) + ',' for i in range(0, len(data), 16))
        if not data: lines.append('    0,')
        lines.append('};')
    for name in ('prime', 'modulus'):
        array(name, value[name])
    for name in ('gs', 'signature'):
        array('ap_' + name, value['ap'][name])
    fields = dict(dh=['private', 'public', 'peer', 'secret'], hashes=['data', 'sha1', 'sha256'],
                  hmac=['key', 'data', 'digest'], transcripts=['secret', 'transcript', 'material', 'challenge', 'tx', 'rx'],
                  ctr=['key', 'iv', 'plaintext', 'ciphertext'])
    for group, names in fields.items():
        records = []
        for i, vector in enumerate(value[group]):
            values = []
            for name in names:
                symbol = f'{group}_{i}_{name}'
                array(symbol, vector[name])
                values += ['cv_' + symbol, str(len(bytes.fromhex(vector[name])))]
            if group == 'ctr': values.append(f'UINT64_C({vector["offset"]})')
            records.append('    {' + ','.join(values) + '},')
        lines.extend([f'static const Crypto{group.capitalize()}Vector cv_{group}[] = {{', *records, '};'])
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(('\n'.join(lines) + '\n').encode())


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--generate', action='store_true')
    group.add_argument('--check', action='store_true')
    group.add_argument('--header', type=Path)
    parser.add_argument('--seed-ap', type=Path)
    args = parser.parse_args()
    if args.header:
        header(args.header)
    else:
        ap = json.loads(args.seed_ap.read_text()) if args.seed_ap else json.loads(FIXTURE.read_text())['ap']
        text = (json.dumps(generate(ap), indent=2) + '\n').encode()
        if args.generate:
            FIXTURE.parent.mkdir(parents=True, exist_ok=True)
            FIXTURE.write_bytes(text)
        elif FIXTURE.read_bytes() != text:
            raise RuntimeError('Crypto fixtures differ from independent oracle')
        print('DH, signed AP, hashes/HMAC, transcript and random-access AES vectors match independent oracle')
