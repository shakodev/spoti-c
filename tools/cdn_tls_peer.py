"""Offline untrusted TLS peer for the C/Schannel transport regression.
Python/OpenSSL is a test oracle only; the application uses native Schannel.
"""
from pathlib import Path
import socket
import ssl

fixtures = Path(__file__).resolve().parents[1] / 'tests/fixtures/audio'
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.minimum_version = context.maximum_version = ssl.TLSVersion.TLSv1_2
context.load_cert_chain(fixtures / 'untrusted-cdn.crt', fixtures / 'untrusted-cdn-key.fixture')
with socket.socket() as listener:
    listener.bind(('127.0.0.1', 0))
    listener.listen(1)
    listener.settimeout(5)
    print(listener.getsockname()[1], flush=True)
    peer, _ = listener.accept()
    with peer:
        peer.settimeout(5)
        hello = peer.recv(5, socket.MSG_PEEK)
        assert len(hello) == 5 and hello[0] == 22
        try:
            with context.wrap_socket(peer, server_side=True):
                raise AssertionError('Schannel accepted the untrusted fixture certificate')
        except ssl.SSLError:
            pass
