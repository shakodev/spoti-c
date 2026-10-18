"""Verify or deliberately regenerate the PUBLIC loopback TLS test identity.
The key is exclusively synthetic test data; no user certificate store writes.
"""
import argparse
import datetime as dt
import hashlib
import ipaddress
import json
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.x509.oid import ExtendedKeyUsageOID, NameOID

root = Path(__file__).resolve().parents[1] / 'tests/fixtures/audio'
parser = argparse.ArgumentParser()
parser.add_argument('--write', action='store_true')
if not parser.parse_args().write:
    manifest = json.loads((root / 'untrusted-cdn.json').read_text())
    for name, record in manifest['files'].items():
        data = (root / name).read_bytes()
        assert len(data) == record['bytes'] and hashlib.sha256(data).hexdigest() == record['sha256']
    print('Public TLS fixture digests verified; no trust or key store changes.')
    raise SystemExit(0)
key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, 'localhost')])
cert = (x509.CertificateBuilder().subject_name(subject).issuer_name(subject)
        .public_key(key.public_key()).serial_number(19)
        .not_valid_before(dt.datetime(2020, 1, 1, tzinfo=dt.timezone.utc))
        .not_valid_after(dt.datetime(2040, 1, 1, tzinfo=dt.timezone.utc))
        .add_extension(x509.SubjectAlternativeName([
            x509.DNSName('localhost'), x509.IPAddress(ipaddress.ip_address('127.0.0.1'))]), False)
        .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
        .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.SERVER_AUTH]), False)
        .sign(key, hashes.SHA256()))
files = {'untrusted-cdn.crt': cert.public_bytes(serialization.Encoding.PEM),
         'untrusted-cdn-key.fixture': key.private_bytes(serialization.Encoding.PEM,
            serialization.PrivateFormat.PKCS8, serialization.NoEncryption())}
for name, data in files.items():
    (root / name).write_bytes(data)
(root / 'untrusted-cdn.json').write_text(json.dumps(dict(
    purpose='PUBLIC untrusted localhost TLS fixture; never account credentials',
    files={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest()) for name,data in files.items()},
    trust_store_changes=False, windows_key_store_changes=False, account_credentials=False), indent=2) + '\n')
