# Shannon cipher and AP session adapter

Issue #6 supplies pure-C streaming encryption and authentication independently
of socket I/O. The application and default tests compile only C17 sources.

## Evidence and provenance

Consulted cspot `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26`,
`cspot/src/Shannon.cpp`, `cspot/include/Shannon.h` and
`cspot/src/ShannonConnection.cpp`; and librespot
`19959512c212cbc48a3ff64382fe204f52c659a7`,
`core/src/connection/codec.rs`. The latter authenticates encrypted
`command:u8 || payload_length:u16be || payload` with a four-byte Shannon MAC,
uses independent TX/RX cipher state, and encodes each packet nonce as u32 big
endian starting at zero. These are source observations, not live server tests.
This module adds no HTTP endpoint.

The algorithm was ported into C from the MIT-licensed
[shannon 0.2.0 crate](https://static.crates.io/crates/shannon/shannon-0.2.0.crate)
used by librespot. Original copyright/license is retained at
`vendor/licenses/shannon/LICENSE`. `vendor/manifest.json` records the original
crate SHA-256 `7ea5b41c9427b56caa7b808cb548a04fb50bb5b9e98590b53f28064ff4174561`,
its `src/lib.rs` hash, and hashes of the modified C port and its header/license.
The C port is explicitly marked as modified. No cspot C++ implementation is
compiled or linked.

The reference crate has a fragmentation quirk in `Shannon::process`: while
finishing a previously buffered word it invokes `macfunc` after each byte, even
when `nbuf` remains nonzero. This disagrees with its own contiguous results for
some splits. The C implementation accumulates the plaintext word and updates
the MAC exactly once when the word is complete, matching contiguous reference
results and cspot's completed-word logic. The reference driver therefore makes
one contiguous encrypt/decrypt call per vector; it remains unmodified upstream
code. Fragmented C results are checked against those independent fixed answers.

## Primitive contract

`vendor/shannon.h` exposes caller-owned fixed `ShannonCtx` state and
`shannon_key`, `shannon_nonce`, `shannon_encrypt`, `shannon_decrypt`,
`shannon_finish`, and `shannon_clear`. Key lengths are 1..32 bytes, nonces 0..16,
and MAC output 1..32. Invalid arguments/lifecycle transitions return false
without changing state. A nonce is required before processing after keying or
finishing. Zero-length data updates are allowed; encrypt/decrypt modes cannot
mix under one nonce. Low-level nonce selection/reuse is the caller's
responsibility; use the adapter for AP nonce allocation.

All arithmetic words are `uint32_t`; rotations use nonzero constant counts below
32. Four-byte words are explicitly loaded/stored as little-endian bytes, with no
pointer punning or unaligned integer access. Partial plaintext words and
keystream offsets persist across arbitrary update boundaries. Finishing pads
the last plaintext MAC word, injects the partial-word marker, folds the CRC into
the stream register, diffuses, and emits the requested MAC bytes.

The context contains key-derived state and must never be logged or copied as an
application value. Every context belongs to one session direction and owning
thread. Buffers must not overlap the context. Clear uses volatile byte stores
over the entire object, including padding; the owning handshake module must
also wipe its original key buffers when no longer needed. There are no heap
allocations, OS handles, locks, or timers.

## AP adapter contract

`shannon_adapter.h` adds explicit argument/state/limit/authentication/exhaustion
outcomes for 32-byte session keys and four-byte MACs. Own two distinct adapters,
one initialized as TX and one RX. Initialize before use, do not modify public
storage members, and clear both at session teardown.

Call `begin`, zero or more in-place `update` calls, then `finish_tx` or
`finish_rx` for the chosen direction. A message is limited to 65,538 plaintext
bytes (three-byte AP header plus at most 65,535 body bytes). This layer does not
parse the header, interpret its length, buffer socket fragments, or dispatch
commands. Those responsibilities belong to the subsequent transport module.

`begin` reserves a fresh nonce immediately. The u64 bookkeeping counter can
represent exhaustion of the u32 wire space without wrapping. A new begin while
active is rejected; abandoning a message requires session teardown. After nonce
`0xffffffff`, the next begin destroys cipher state and returns exhaustion.
Reinitialization requires a new session/key exchange; never reset counters with
the same keys to retry a connection.

RX updates produce tentative plaintext. The caller must retain it in quarantine
until `finish_rx` authenticates it; on failure it must discard/wipe the staging
buffer and close the session. The four-byte comparison reads every MAC byte
without a data-dependent early exit. Authentication failure wipes the adapter's
key-derived state and latches FAILED, rejecting further processing. Invalid
calls or capacity overflow leave state unchanged and do not silently truncate.

## Independent known-answer tests

`tests/fixtures/shannon-vectors.json` records 18 complete key/nonce/plaintext/
ciphertext/MAC tuples plus reference version, crate checksum, locked dependency
and driver hashes. It includes zero/nonzero/max-u32 nonces, zero input, lengths
on either side of word boundaries, high-bit bytes, partial key/nonce/MAC words,
an empty-body frame, consecutive frame nonces, an odd body, and the full
65,538-byte maximum AP plaintext. Full maximum-frame ciphertext is retained,
not only a digest or a roundtrip result.

The optional oracle is independently runnable:

```powershell
python tools/shannon_vectors.py --check-reference
# To intentionally regenerate the checked-in fixture:
python tools/shannon_vectors.py --generate
```

It runs `tools/shannon_reference` using Cargo `--locked`, with shannon 0.2.0 and
byteorder 1.5.0 checksum-pinned in Cargo.lock. Rust and first-run crate downloads
are needed only for this explicit reference check; all artifacts go into
ignored `.cache/shannon-reference`. The test-only Rust driver is never a CMake
translation unit or runtime dependency.

CMake converts the checked-in JSON to a test-only C header using Python's
standard library. This checks the driver/lockfile hashes and the C port's vendor
manifest hashes; it does not execute Cargo or access the network. Build/test:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON
cmake --build build
ctest --test-dir build -R "^(protocol_shannon)$" --output-on-failure
ctest --test-dir build --output-on-failure
```

Use the established x64 developer environment and pinned protobuf Python tools.
Tests compare every ciphertext, plaintext and MAC byte to the reference for
contiguous and fragmented encryption/decryption, every two-part split on small
inputs, byte-at-a-time input, varied chunk sizes, and buffer offsets 0..3. Canary
checks supplement ASan. They flip every bit of an eight-byte frame's command,
length, body and four-byte MAC (96 cases), require authentication failure and
destroyed key state, and test direction isolation, nonce progression/exhaustion,
capacity boundaries, invalid calls and teardown erasure. A debug CRT allocation
hook verifies zero heap calls throughout the C tests. There are no handles to
leak. All production/test C uses the existing `/W4 /WX` and Debug ASan options.
