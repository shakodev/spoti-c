# Bounded Spotify protobuf codec

Issue #5 uses the 38-file closure recorded in `proto/manifest.json`, copied byte
for byte from librespot `19959512c212cbc48a3ff64382fe204f52c659a7`. Import paths
and original schemas are retained, including legacy SPIRC, modern Connect and
transfer/queue, extended metadata, storage, clienttoken, Login5, Any and Duration.
`proto/LICENSE.librespot` retains the upstream MIT license. This implements wire
serialization only; it adds no HTTP endpoint or claim of live compatibility.

The reference files consulted were librespot's `protocol/build.rs`,
`protocol/proto/{keyexchange,authentication,metadata,connect}.proto`, the complete
closure in `docs/specs/proto-inventory.json`, cspot's `cspot/protobuf/spirc.options`
and `cspot/bell/external/nanopb`. cspot's pointer options are deliberately replaced
with static bounds or callbacks. The full DH challenge preserves
`server_signature_key` and `gs_signature` for later AP authentication verification.

## Generation and provenance

Runtime and generator are vendored nanopb **0.4.9.1**, with original license and
per-file SHA-256 hashes in `vendor/manifest.json`. Build-time protoc is
**libprotoc 28.1**, supplied by **grpcio-tools 1.68.1**, with **protobuf 5.28.3**.
Only Python build tools depend on protobuf/grpc; the application links three
nanopb C runtime sources and generated C descriptors. There is no C++ translation
unit, protobuf C++ runtime, or dependency on `ref/` during a build.

Install the tools in a local virtual environment, then configure from an x64
Visual Studio developer PowerShell:

```powershell
uv venv .cache/proto-tools --python 3.12
uv pip install --python .cache/proto-tools/Scripts/python.exe -r tools/proto-requirements.txt
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DPython3_EXECUTABLE="$PWD/.cache/proto-tools/Scripts/python.exe"
cmake --build build
ctest --test-dir build --output-on-failure
python tools/check_proto.py --build build --audit
```

Use the virtual environment's Python for the last command (or activate it).
CMake lists every schema, options file, manifest and generator source as an
explicit generation dependency. Outputs live under `build/generated`, including
all 76 `.pb.c`/`.pb.h` files and the descriptor set. The generator checks source
hashes and compiler versions, disables timestamps, and builds its Python helper
in temporary build storage without modifying the source tree. `.gitattributes`
preserves manifest-covered bytes across Git checkouts. Builds need no network
after installing the pinned tools.

`protocol_proto_codegen` regenerates twice into independent temporary directories,
compares all output bytes, and checks the exact import closure. `--audit` also
checks every compile command is C and uses `dumpbin /imports` on the protocol test
and `/directives` on the library to reject C++ runtime dependencies.

## Memory, wire limits and ownership

`proto_codec.h` exposes encode, decode, descriptor-aware validation and selected
Track decoding. No runtime malloc, handles, recursive metadata trees, `FT_POINTER`
or `PB_ENABLE_MALLOC` are used. Fixed auth structs are bounded by `.options`;
repeated metadata, Connect maps/queues and storage URLs use callbacks. Initialize
generated structs with their `*_init_zero` macro before assigning callbacks.
Callers must explicitly install callbacks for fields they want to retain or emit;
unassigned callbacks discard fields. Metadata country-restriction and token/Login5
oneofs use static union members so the last supported wire alternative wins.

Validation runs before nanopb decoding, including through discarded known
submessages. Limits are 1 MiB input, eight message levels (root = one), 100 entries
per repeated field (packed values and maps count individually), 4095 wire bytes
per known UTF-8 string and 4096 per blob. Static field options can impose smaller
limits, including auth repeated lists of eight and 96-byte DH values. Known text
rejects embedded NUL. Valid unknown varint/fixed32/fixed64/length-delimited fields
are skipped by wire type; their opaque contents are not interpreted recursively.
Groups, invalid wire types, overflowing/truncated varints, lengths, missing required
fields, invalid UTF-8, and wrong known field wire types are rejected. These are
explicit resource limits, not claims about all possible Spotify payload sizes.

`proto_decode_track` selects GID, name, canonical URI, album name, artist GID/name,
duration, explicit flag and audio file ID/format. It allocates only retained
strings and linked records in a caller-owned initialized arena. GIDs/file IDs
must be exactly 16/20 bytes; retained names and URIs cap at 256 bytes, artists and
files at 100 each. Discarded recursive alternatives are still validated. A failed
decode rewinds to its entry arena mark and leaves the output unchanged. Success
pointers remain owned by the caller's arena until rewind/reset/disposal. The
caller must select supported formats and apply restrictions in later modules.

Generic encode/decode has explicit invalid-argument, malformed, resource-limit
and arena-exhaustion results. Discard a generic destination on failure, since
nanopb may have partially populated it. Encoding sets `written` only on success;
discard the output buffer on failure. Nothing silently grows or truncates.

## Verification

`protocol_proto` contains fixed golden bytes for auth, AP signatures, Mercury
Header, Track/Album/Artist, PutStateRequest with DeviceInfo, transfer with queue,
clienttoken, Login5 stored credentials and StorageResolveResponse. It checks
oneof replacement in both scalar/submessage directions and metadata restrictions,
unknown-field skipping, discarded nested malformed data, exact/exceeded depth,
packed and unpacked item counts, name/URI/auth bounds, arena rollback, truncated
fields, and 5,000 deterministic mutations. The MSVC debug CRT allocation hook
checks zero heap calls across the codec tests; arenas are disposed before their
backing storage leaves scope. The full Debug suite runs with ASan and `/W4 /WX`.
