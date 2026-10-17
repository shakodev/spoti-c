# Windows CNG protocol cryptography

Issue #7 implements independent C17 primitives for AP authentication and audio
decryption. The production module links only Windows `bcrypt.lib` and the
project's strict C build options. No HTTP endpoint, socket transport, login flow,
or audio decoder is implemented here.

## Protocol evidence

Consulted librespot `19959512c212cbc48a3ff64382fe204f52c659a7`:
`core/src/diffie_hellman.rs` (768-bit prime, generator 2, minimal unsigned big-endian
public/shared bytes), `core/src/connection/handshake.rs` (pinned RSA modulus,
exponent 65537, PKCS#1 v1.5/SHA-1 signature over the exact received `gs`, transcript
HMAC schedule), and `audio/src/decrypt.rs` (`Ctr128BE<Aes128>` and audio IV
`72e067fbddcbcf77ebe8bc643f630d93`). Also inspected cspot
`1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`, `cspot/src/AuthChallenges.cpp`.
The latter's missing AP signature verification is not carried forward.
Public constants retain the source attribution and existing MIT license at
`proto/LICENSE.librespot`.

Microsoft documents the parameter and import blob formats in
[Diffie-Hellman keys](https://learn.microsoft.com/en-us/windows/win32/seccrypto/diffie-hellman-keys).
The [BCryptDeriveKey contract](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekey)
specifies that `BCRYPT_KDF_RAW_SECRET` returns little-endian bytes. These are
reversed and stripped of big-endian leading zeros before transcript HMAC.

## Ownership and failures

Initialize a caller-owned `Crypto` with `crypto_init`; start each `CryptoDh` at
`{0}`. Each owner is confined to one thread. Clear every DH object before reuse
or owner destruction, and never reinitialize an owner while it holds handles.
Never copy live contexts. Input buffers must not overlap outputs or contexts,
except the documented in-place AES data buffer.

The module makes no application heap allocations. CNG provider internals may
allocate memory. All algorithm, key, hash and secret handles are counted in the
owner ledger, with a hard cap of 16 live handles. A DH context retains two; normal
individual operations peak at at most four including an existing DH context.
`crypto_balanced` requires zero handles and no reported cleanup error. Cleanup
calls always reach CNG; failures are latched and cannot be mistaken for balanced
ownership. These operations belong on a worker thread, outside UI rendering and
the WASAPI callback.

Hash/AES objects use fixed, 16-byte-aligned 1024-byte storage, guarded by the
provider's object-size query. Larger provider requirements return `CRYPTO_LIMIT`.
Temporary secrets, key schedules, HMAC material, raw DH bytes and keystream blocks
are erased with `SecureZeroMemory` after use, including provider-error exits.
Returned shared secrets and session keys belong to the caller, which must wipe
them after handing keys to their session owners.

Errors distinguish arguments, state, limits, invalid peers, authentication and
provider failure. Digest/key outputs are zero on failure; shared-secret/public
outputs are fully zero with length zero. AES provider errors wipe the whole
in-place data buffer, so it must be discarded/refetched. Argument and range
errors leave AES data unchanged. No operation silently grows or truncates.

## DH and pinned AP authentication

`crypto_dh_init` uses the Microsoft Primitive Provider to generate a fresh CNG
768-bit DH key pair, installs the exact custom P/G before finalization, and
retains the provider/private-key handles. `crypto_random` exposes CNG's system
RNG for other protocol randomness. Production code never imports a deterministic
private exponent; fixture-key import exists only in test code.

Public exports validate the returned P/G and public value, then emit minimal
big-endian bytes. Peers must occupy 1..96 wire bytes and satisfy `2 <= y <= p-2`.
They are left-padded only for the CNG DH public blob. Signature hashing uses the
original received wire bytes, including any leading zeros.

`crypto_ap_verify_signature` imports only the pinned 2048-bit RSA public key and
verifies a 256-byte PKCS#1 v1.5/SHA-1 signature. The public `crypto_dh_derive` API
requires that signature and verifies it before importing a peer or performing
secret agreement. Unauthenticated AP values never produce a shared secret through
this API. An internal raw derive helper supports independent mathematical tests;
it is not part of the public header or the application authentication contract.

The 96-byte CNG raw secret must be returned in full. Convert it from little
endian to minimal unsigned big endian, rejecting a degenerate zero/one result.
`crypto_ap_derive_keys` accepts that verified minimal secret and at most 128 KiB
of the exact framed ClientHello/APResponse transcript. It rejects leading-zero
padding and computes five `HMAC-SHA1(secret, transcript || counter)` blocks for
counter bytes 1..5, then challenge `HMAC-SHA1(D[0:20], transcript)`, TX `D[20:52]`
and RX `D[52:84]`. Plain `crypto_sha1` and `crypto_sha256` remain separate APIs
from `crypto_hmac_sha1`, including for later hashcash use.

## Seekable audio AES-CTR

`crypto_ctr_xor_at(key, iv, file_offset, bytes, length)` uses AES-128 in CNG ECB
mode without padding to encrypt big-endian counter blocks. Initial counter is
`iv + floor(file_offset/16)`; XOR begins at byte `file_offset % 16`. Up to 64
counter blocks are encrypted per call to CNG. Full 128-bit carry is supported.
Byte-offset and counter-space overflow are rejected before modifying data.

Offsets address the original encrypted file. Logical Vorbis offset zero is at
encrypted offset 167; the caller supplies that translation and must not restart
the counter at the Ogg payload. CTR is implemented explicitly; no unsupported
CNG CTR chaining-mode property is requested.

## Fixtures and verification

`tests/fixtures/crypto-vectors.json` contains independent Python integer `pow`
DH vectors (including one-byte public/shared values and a 95-byte shared secret),
plain SHA-1/SHA-256 vectors, RFC 2202 HMAC cases, complete transcript schedules,
NIST SP 800-38A F.5.1 AES-128 CTR and audio random-access/carry vectors.

The positive AP signature fixture is a public unauthenticated hello captured from
`ap-gew4.spotify.com:4070` at the UTC time stored in the JSON. Exact request and
response bytes, `gs`, signature and returned key ID 0 are preserved. The socket
was closed after receiving the challenge; no credentials or login were sent.
The independent oracle verified that signature against the pinned modulus before
generating the fixtures. This is evidence of the public handshake, not evidence
of authenticated account playback compatibility.

Normal CMake builds convert the JSON into a test-only C header using Python's
standard library, with no network access or additional crypto runtime. To rerun
the optional independent oracle, install the pinned `tools/crypto-requirements.txt`
in a local Python environment and run:

```powershell
python tools/crypto_vectors.py --check
```

This uses Python `pow`/`hashlib`/`hmac` and cryptography 46.0.3, never the CNG module
under test. Re-generation reuses the stored public AP capture and requires no
network. `--generate` intentionally updates expected fixtures.

Build and run in the established x64 Visual Studio developer environment:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build -R "^(protocol_crypto)$" --output-on-failure
ctest --test-dir build --output-on-failure
```

Tests cover real CNG custom-group generation/agreement, exact independent public
and minimal-secret bytes, peer endpoints/overflow, genuine/tampered AP signatures
and DH values, fail-closed verified derive, separate plain hashes and HMAC,
transcript keys, and contiguous/split AES at offsets 0/1/15/16/17/166/167. Carry
tests cross the low 64-bit counter boundary, exercise the last legal 128-bit
counter byte and last uint64 file byte, and reject overflow. Unaligned buffers
and canaries supplement ASan.

A per-owner internal fault seam rejects each fallible provider operation in turn,
including late AES batches and transcript blocks; tests require wiped outputs
and balanced handle ledgers on every path. Cleanup is never faulted into leaking
an otherwise valid handle. The hard handle cap is tested with eight live DH
contexts. The debug CRT hook verifies zero application heap calls.

Verified on x64 Windows build **10.0.26200.9711** using the Microsoft Primitive
Provider, including successful 768-bit custom parameters and RAW_SECRET. Other
Windows/provider policies are not claimed tested: unsupported capabilities return
an explicit provider error. All 14 Debug ASan tests passed with `/W4 /WX`; compile
commands and crypto binary imports/directives were inspected for C-only linkage.
