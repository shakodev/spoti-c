# Pure-C Spotify client architecture and execution roadmap

Originally inspected and planned on **2026-10-17** for `shakodev/spoti-c`.
The backlog contains **35 implementation issues across six phase labels and
milestones**, ordered by prerequisites. It covers a pure-C native Windows client,
authenticated Vorbis playback, modern Connect, internal data APIs, GPU UI, OS
integration and measurable release gates.

## Implementation status reviewed 2026-10-19

GitHub issues **#1–#30 are implemented**; **#31–#35 remain open**. Current CMake builds
`spotic`, `spotic-headless` and the module tests. The default Debug/ASan suite
contains 48 tests. Core/protocol/audio/API and UI-engine modules have landed;
the GUI composes the responsive Home/Library shell and synchronized playback
bar. Its explicit offline demo uses existing collection/player services; a live
account transport host is not configured in the standalone executable. Additional
screens, SMTC, startup state persistence and final full-client release gates
remain open. See [shell verification](verification/app-shell.md).
The targets below are not claims that final resident RAM, startup or executable
size thresholds already pass.

The architecture, allocation ledger and dependency table below preserve the
initial plan. Actual module contracts take precedence where implementation has
changed. In particular, CDN audio uses the native Winsock/Schannel transport
documented in [cdn-transport.md](specs/cdn-transport.md); authentication and
internal APIs still use WinHTTP. Later evidence resolves initial request gates
in [auth-tokens.md](specs/auth-tokens.md),
[playback-resolution.md](specs/playback-resolution.md),
[pathfinder.md](specs/pathfinder.md) and
[native-library-playlist.md](specs/native-library-playlist.md). The initial
memory ledger remains a design reservation; use module specifications and
[headless acceptance](verification/headless-acceptance.md) for measured scope.

`backlog.json` and `issue-map.json` are the unchanged original issue manifest
and mapping, not live status records. On this review, all 35 titles, complete
bodies (after newline normalization), labels and milestones still matched
GitHub. Every dependency points to an earlier issue. The dated
`backlog-verification.json` records the original smoke-only baseline; it does
not describe the present build. Future changes should update the appropriate
issue and implemented contract rather than silently replacing historical evidence.

## Evidence and confidence

The initial planning pass read `AGENTS.md`, `README.md`, `CMakeLists.txt`,
vendored interfaces and local references before drafting issues. The following
evidence table and unresolved-gate descriptions record that pass's scope.

| Reference | Inspected identity | Evidence scope |
|---|---|---|
| cspot | `1b07a9c00ba6d5e878e5b33dcbf89bff493cde26` | AP handshake/framing, nanopb options, Mercury, AudioKey, CDN audio and legacy SPIRC |
| librespot | `19959512c212cbc48a3ff64382fe204f52c659a7` | Full schemas; authenticated AP; OAuth/Login5/clienttoken; modern metadata/storage/Dealer; streaming/decode/seek/gapless |
| XPUI | Local manifest Spotify `1.3.4.258`, archive SHA-256 `19be39b9f8da51a5e766699bc0e7a8bf6dc6f09b1b733a2d8feb4cb2e1ce0608` | REA 6.5.0 statically analyzed 16 selected JS entry/shared files, zero parse failures; source searches across top-level JS supplement the analysis |
| Vendors | `vendor/manifest.json` pins versions/digests | Clay memory/measurement, stb_vorbis push API, miniaudio event-driven WASAPI, yyjson in-situ allocator, stb_image formats |

Detailed source anchors and exact wire conventions are in [transport-and-playback.md](specs/transport-and-playback.md). [proto-inventory.json](specs/proto-inventory.json) records the actual 38-file transitive import closure. [xpui-static.md](specs/xpui-static.md) and [xpui-evidence.json](specs/xpui-evidence.json) preserve query literals, file digests/offsets, REA Evidence IDs and limitations. Large full REA outputs remain in ignored `.cache/`; checked-in summaries make the boundaries reviewable.

**Observed does not mean live-verified.** No account session or Spotify runtime traffic was captured in this analysis. XPUI's main loader uses a preloaded `__webpack_modules__` table; several auth/request modules are not defined in the extracted JS. Search and artist documents and some consumer shapes are present; generic PathFinder HTTP assembly, primary Home/Playlist/Album queries, library and lyrics contracts remain partially unresolved. Pass one also hit a semantic graph result budget for `9806.js`, retaining static/module facts. These gaps are explicit implementation gates in issues 11 and 21, not guessed endpoint specifications. Resolver/OAuth/playback-resolution/Connect issues also require REA spec verification before their endpoint implementation.

## Architecture synthesis

The executable is a set of bounded services with one owner each. Pure C headers expose opaque handles, fixed records and explicit result codes. Generated nanopb C handles binary wire messages; yyjson handles bounded JSON documents. WinHTTP supplies TLS/HTTP/WebSocket; Winsock supplies raw AP TCP. Windows CNG supplies RNG, DH, hashes, RSA verification and AES block encryption. No C++ translation units or standard-library runtime are allowed, including for DX11, DirectWrite, WIC or SMTC: use their C ABI/vtables. Keep project source modules under 500 lines; shaders are build-time compiled assets.

### Session and playback path

1. Acquire OAuth via PKCE or load DPAPI-protected reusable credentials. Resolve `accesspoint`, `dealer`, `spclient`; open raw AP TCP. AP port 443 does not imply TLS.
2. Exchange DH ClientHello/APResponseMessage with exact big-endian lengths and full transcript. Verify AP RSA signature. Derive client HMAC challenge and independent 32-byte TX/RX Shannon keys from five HMAC-SHA1 blocks; send ClientResponsePlaintext.
3. Shannon frame is encrypted command/u16-length/payload plus 4-byte MAC, with independent big-endian u32 nonce sequences. Authenticate encrypted Login/APWelcome, preserve canonical username and returned reusable credential, maintain keepalives/reconnect. Reject bad MACs before dispatch.
4. Provide Mercury requests/subscriptions as a distinct compatibility service. Current metadata uses spclient extended metadata, not solely Mercury. Clienttoken then Login5 bearer supplies internal HTTP credentials; OAuth/AP/blob/clienttoken/bearer are distinct objects and lifetimes.
5. Resolve restricted/alternative playable Track and select Vorbis file ID. AudioKey is a binary AP request (`0c`, 42 bytes), not Mercury/protobuf. Resolve expiring CDN URL through authenticated storage service. CDN requests receive range headers but no account bearer credentials.
6. Fetch bounded encrypted ranges into a RAM block pool and disk cache. AES-128-CTR uses absolute encrypted offset and IV `72e067fbddcbcf77ebe8bc643f630d93`. Decrypt before removing the 167-byte Spotify prefix; logical Ogg offset zero still maps to encrypted offset 167.
7. Push-decode Vorbis into preallocated PCM; a bounded Ogg granule index supplies seek/resync/pre-roll. WASAPI consumes a PCM SPSC ring. Prefetch at most one next track and transition on an exact PCM boundary through the same compatible sink.
8. Modern Spotify Connect uses Dealer WebSocket plus HTTPS connect-state, transfer/queue protobufs and acknowledged player commands. cspot's Mercury `spirc.proto` route is documented as legacy, not the primary contemporary implementation.

### Important reference corrections

- cspot's reduced `keyexchange.proto` omits the AP signature; its Shannon connection logs a MAC mismatch and continues. Both behaviors are unsuitable here; follow librespot's fail-closed verification.
- The current schema file is `connect.proto`, with package `spotify.connectstate`. Track/Album/Artist all reside in `metadata.proto`.
- CNG raw DH secrets require endian conversion and leading-zero normalization; CTR is built from AES-ECB counter encryption, not an assumed CNG CTR property. See the linked Microsoft references in the transport spec.
- `stb_vorbis` push mode does not implement sample seeking; valid Vorbis packets can exceed any chosen fixed budget. Implement seeking and document explicit packet/setup rejection limits.
- `stb_image` handles JPEG/PNG but not WebP. Use optional capability-tested WIC WebP decoding with an evidenced JPEG alternative or placeholder. A new C WebP library would require a deliberate size/memory review.
- `YYJSON_READ_INSITU` avoids string copying; DOM nodes still need memory. Borrowed spans remain valid only while their owning response page is retained.
- DirectWrite and driver allocations cannot be wished away. Prewarm/cache shaping outside rendering, account OS memory separately and measure full resident usage.

## Thread, ownership and wake model

| Execution context | Owns | Input/output and sleep policy |
|---|---|---|
| UI thread | HWND, Clay arena/frame scratch, renderer, font/texture handles, route state | `MsgWaitForMultipleObjectsEx`; input + coalesced completion event + nearest justified timer. Render only when dirty. Never block for network, decode or disk. |
| Network/protocol thread | AP socket/ciphers/nonces, token manager, HTTP slots, Mercury/AudioKey, source resolution, Dealer | Wait on socket/completion events and deadlines. Owns serialization/retry; hands off immutable bounded pages/blocks via handles and generations. |
| Audio decode thread | current/next Vorbis arenas, conversion, Ogg index, PCM producer, playback state transitions | Sleeps on commands/data availability/watermarks; no busy polling. Does not reset live consumer indices; use quiescence/epoch acknowledgement. |
| WASAPI/miniaudio callback thread | PCM consumer cursor and cheap counters | OS event-driven. Bounded frame copy/gain/silence plus a dedicated low-water edge `SetEvent` to wake decoder. No malloc, lock, decode, HTTP/disk, log formatting or per-period UI wake. |
| One blocking I/O/art worker | bounded image decode jobs, cache disk operations and log flushing | Fixed job pool, sleeps on work event. Prioritize audio disk reads over artwork and never block the audio callback. |
| WinHTTP/OS callbacks | no application mutable model ownership | Copy bounded completion descriptor into MPSC queue, signal owner, retain request slot until callback close lifecycle ends. |

The fifth application worker is intentional: disk/image work must not stall AP keepalives or the decode lane. WinHTTP and audio may create OS-owned threads; count their resident stacks/allocations. Do not promise a fixed process-wide thread count. The low-water audio event must not lose a wake during refill and must never route through the UI or a locking MPSC queue. Playback/lyrics/SMTC use media frames corrected for device/output latency and exclude underrun silence; frames merely copied into WASAPI are ahead of audible output.

Use SPSC only where producer and consumer are each unique (decoder→WASAPI, optional dedicated command lanes). Multiple WinHTTP callbacks, SMTC and worker producers require bounded MPSC handoff or isolated SPSC lanes. A manual-reset wake event must be reset atomically with the empty-queue decision to prevent lost wakes. Every request/job/texture/page carries a generation so cancelled work cannot populate a reused slot. Shutdown stops producers, cancels callbacks, joins workers, releases retained pages/COM/WinHTTP handles, then clears arenas/secrets.

UI progress updates at most once per second while playing. Hover/scroll may run at 60 Hz only during an active transition; the timer is removed afterward. Visible synchronized lyrics may arm a one-shot next-line deadline. Paused/hidden/minimized surfaces do not run progress/animation timers unnecessarily. Network keepalives and audio callback events are independent of rendering and remain necessary.

## Initial memory ledger

These are **design reservations to validate**, not measured success. KiB/MiB below are binary units; acceptance targets in the issues use decimal bytes. Reserved virtual address space is not resident memory. Shared pools must not be counted once per consumer.

| Headless-owned allocation group | Initial committed/high-water cap | Owner |
|---|---:|---|
| Core queues, jobs, logging and telemetry | 128 KiB | Core owners |
| AP wire/transcript buffers and crypto/proto scratch | 256 KiB | Network |
| Metadata, pending Mercury/AudioKey/Connect and cache index pools | 256 KiB | Network; sublimits include 128 KiB multipart |
| HTTP slots/transport runtime allowance (excluding payload pools) | 384 KiB | Network/WinHTTP |
| Encrypted block pool, eight 64 KiB blocks | 512 KiB | Network; pinned leases to decoder |
| Two decoder arenas | 512 KiB | Decode |
| Compressed window/Ogg index/conversion scratch | 128 KiB | Decode |
| Interleaved PCM ring | 256 KiB | Decode producer/WASAPI consumer |
| miniaudio device/conversion allowance | 128 KiB | Audio device |
| **Headless explicit allocation subtotal** | **2,560 KiB (2.50 MiB)** | |

| Additional GUI allocation group | Initial cap |
|---|---:|
| Clay arena and frame scratch | 1,280 KiB |
| Two API response/yyjson page slots, combined | 1,024 KiB |
| Compact model/track/filter indices, shared across screens | 512 KiB |
| Widget/app/navigation persistent state | 256 KiB |
| Renderer instance/staging buffers | 512 KiB |
| Font CPU scratch/cache | 256 KiB |
| Artwork decode plus upload staging | 768 KiB |
| Binary startup state snapshot | 256 KiB |
| **GUI increment / full explicit subtotal** | **4,864 KiB (4.75 MiB) / 7.25 MiB** |

Additional GPU-owned caps: **4 MiB artwork textures + 1 MiB R8 glyph atlas**. Swapchain/backbuffers, driver resources, shared GPU mappings, OS libraries, CNG/WinHTTP/DirectWrite internals and committed thread stacks are measured separately and still count when resident in the process. A 1920×1080 RGBA buffer alone is about 7.9 MiB of graphics allocation; multiple backbuffers and shared mappings are a real risk to the full-client target. The 7.25 MiB subtotal is not evidence of <20 MB residency. The headless <8 MB target likewise leaves only about 5.4 MB decimal for unlisted overhead.

Backpressure is a feature: full pools cancel obsolete work, limit read-ahead, evict unpinned art/pages, or return a clear budget error. Never silently increase caps. Verify decoder setup/packet budgets using a representative corpus before calling them sufficient. A <=1 MiB HTTP body limit is only an outer bound; yyjson DOM plus padded input must fit the shared page pool, so actual accepted JSON may be smaller. Large playlists retain compact IDs/disk indices plus visible metadata pages, not all JSON or Clay nodes.

## Performance and correctness gates

Debug correctness: `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON`, `cmake --build build`, `ctest --test-dir build --output-on-failure`. New tests inherit `/W4 /WX` or `-Wall -Wextra -Werror`. Add explicit allocation and handle balance checks: Windows ASan alone is not a leak detector or concurrency proof. Register `bench_*` only in Release/non-ASan configurations. Live account/audio/optional GPU tests require explicit CMake opt-in; a CTest label alone does not remove them from the default suite. Offline fake-device tests remain enabled and deterministic; skipped live checks are reported honestly.

Release measurements: configure/build `build-rel` with `-DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF`; run named benchmark CTests there. Collect raw process CPU time, total/peak working set, private working set/commit, allocation high-water, callback/wake/present counts, handles and GPU allocations. Do not trim the working set, hide work in another process, or exclude DLL/driver residency to pass.

| Goal | Acceptance definition |
|---|---|
| Headless RAM | <8,000,000 bytes peak resident working set in documented 10-minute 320-kbps playback with seek/gapless workload |
| Full client RAM | <20,000,000 bytes peak resident working set over idle/playing/navigation/filter/art-churn workload on pinned reference system |
| Idle/playing CPU display | Target displayed 0.0%; report raw CPU and exact rounding threshold. Literal zero computation while decoding/outputting audio is impossible. Zero busy polling and no idle presents are strict behavioral gates. |
| Executable | <2,000,000 bytes Release EXE; also report complete distribution/imported DLL requirements, no C++ runtime |
| Cold startup | <30 ms process creation→first useful presented shell on specified machine; report p50/p95, actual cold versus warm file-cache conditions, and later online-ready time separately |
| Interaction | 10k indexed-track filter <=16 ms on reference machine; layout cost proportional to visible rows, frame allocation count zero |

The strict RAM/startup/CPU targets may require tradeoffs after measurements, particularly DirectWrite/WinHTTP/driver overhead. Keep thresholds visible and report failure; do not redefine a target into an application-arena-only or warmed-cache result. Timing acceptance is evaluated in Release; all associated correctness tests also run under ASan.

## Dependency-ordered execution

The six phase labels are organizational workstreams, not artificial barriers. Issues 08 and 11 carry `phase-4-api` but run before audio because authenticated storage resolution needs them. Issue 20 carries `phase-2-protocol` but follows the player because Connect must execute real player commands. Rendering can be developed against fixtures once its prerequisites are done. The entire order is a topological sort; every listed dependency points backward.

Each issue has a clear goal, exact source references, planned C/header/test paths, interfaces/ownership/caps, and named ASan/benchmark acceptance. [backlog.json](backlog.json) is the complete reproducible specification, and [issue-map.json](issue-map.json) maps order to created GitHub numbers/URLs. Dependencies in GitHub issue bodies use actual issue numbers.

<!-- BACKLOG_TABLE_START -->
| Order | GitHub issue / deliverable | Phase | Direct prerequisites |
|---:|---|---|---|
| 01 | [[01/Core] Implement fixed-capacity Arena and Frame Scratch Allocators](https://github.com/shakodev/spoti-c/issues/1) | `phase-1-core` | None |
| 02 | [[02/Core] Implement bounded lock-free SPSC audio and message rings](https://github.com/shakodev/spoti-c/issues/2) | `phase-1-core` | [#1](https://github.com/shakodev/spoti-c/issues/1) |
| 03 | [[03/Core] Implement bounded event queues and Win32 worker wakeups](https://github.com/shakodev/spoti-c/issues/3) | `phase-1-core` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#2](https://github.com/shakodev/spoti-c/issues/2) |
| 04 | [[04/Core] Add structured logging, allocation telemetry and deterministic test fixtures](https://github.com/shakodev/spoti-c/issues/4) | `phase-1-core` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#3](https://github.com/shakodev/spoti-c/issues/3) |
| 05 | [[05/Protocol] Integrate nanopb and the exact bounded Spotify protobuf closure](https://github.com/shakodev/spoti-c/issues/5) | `phase-2-protocol` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#4](https://github.com/shakodev/spoti-c/issues/4) |
| 06 | [[06/Protocol] Port Shannon to pure C with known-answer and tamper tests](https://github.com/shakodev/spoti-c/issues/6) | `phase-2-protocol` | [#4](https://github.com/shakodev/spoti-c/issues/4) |
| 07 | [[07/Protocol] Implement CNG DH, AP signature verification and seekable AES-CTR](https://github.com/shakodev/spoti-c/issues/7) | `phase-2-protocol` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#4](https://github.com/shakodev/spoti-c/issues/4) |
| 08 | [[08/API] Build a bounded WinHTTP asynchronous transport and connection pool](https://github.com/shakodev/spoti-c/issues/8) | `phase-4-api` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#3](https://github.com/shakodev/spoti-c/issues/3), [#4](https://github.com/shakodev/spoti-c/issues/4) |
| 09 | [[09/Protocol] Implement ApResolve and the authenticated Shannon AP connection](https://github.com/shakodev/spoti-c/issues/9) | `phase-2-protocol` | [#3](https://github.com/shakodev/spoti-c/issues/3), [#5](https://github.com/shakodev/spoti-c/issues/5), [#6](https://github.com/shakodev/spoti-c/issues/6), [#7](https://github.com/shakodev/spoti-c/issues/7), [#8](https://github.com/shakodev/spoti-c/issues/8) |
| 10 | [[10/Protocol] Add OAuth PKCE login and DPAPI-protected reusable credentials](https://github.com/shakodev/spoti-c/issues/10) | `phase-2-protocol` | [#7](https://github.com/shakodev/spoti-c/issues/7), [#8](https://github.com/shakodev/spoti-c/issues/8), [#9](https://github.com/shakodev/spoti-c/issues/9) |
| 11 | [[11/API] Verify auth contracts and implement clienttoken and Login5 bearer lifecycle](https://github.com/shakodev/spoti-c/issues/11) | `phase-4-api` | [#5](https://github.com/shakodev/spoti-c/issues/5), [#7](https://github.com/shakodev/spoti-c/issues/7), [#8](https://github.com/shakodev/spoti-c/issues/8), [#10](https://github.com/shakodev/spoti-c/issues/10) |
| 12 | [[12/Protocol] Implement bounded Mercury requests, subscriptions and multipart events](https://github.com/shakodev/spoti-c/issues/12) | `phase-2-protocol` | [#5](https://github.com/shakodev/spoti-c/issues/5), [#9](https://github.com/shakodev/spoti-c/issues/9), [#10](https://github.com/shakodev/spoti-c/issues/10) |
| 13 | [[13/Protocol] Resolve playable metadata, AudioKeys and expiring CDN locations](https://github.com/shakodev/spoti-c/issues/13) | `phase-2-protocol` | [#5](https://github.com/shakodev/spoti-c/issues/5), [#9](https://github.com/shakodev/spoti-c/issues/9), [#10](https://github.com/shakodev/spoti-c/issues/10), [#11](https://github.com/shakodev/spoti-c/issues/11), [#12](https://github.com/shakodev/spoti-c/issues/12) |
| 14 | [[14/Audio] Implement HTTP range fetching and bounded encrypted audio caching](https://github.com/shakodev/spoti-c/issues/14) | `phase-3-audio` | [#2](https://github.com/shakodev/spoti-c/issues/2), [#3](https://github.com/shakodev/spoti-c/issues/3), [#8](https://github.com/shakodev/spoti-c/issues/8), [#13](https://github.com/shakodev/spoti-c/issues/13) |
| 15 | [[15/Audio] Implement random-access stream decryption and Spotify Ogg prefix mapping](https://github.com/shakodev/spoti-c/issues/15) | `phase-3-audio` | [#7](https://github.com/shakodev/spoti-c/issues/7), [#14](https://github.com/shakodev/spoti-c/issues/14) |
| 16 | [[16/Audio] Integrate bounded stb_vorbis push decoding and sample-accurate seeking](https://github.com/shakodev/spoti-c/issues/16) | `phase-3-audio` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#2](https://github.com/shakodev/spoti-c/issues/2), [#14](https://github.com/shakodev/spoti-c/issues/14), [#15](https://github.com/shakodev/spoti-c/issues/15) |
| 17 | [[17/Audio] Implement an event-driven miniaudio WASAPI sink](https://github.com/shakodev/spoti-c/issues/17) | `phase-3-audio` | [#2](https://github.com/shakodev/spoti-c/issues/2), [#3](https://github.com/shakodev/spoti-c/issues/3), [#4](https://github.com/shakodev/spoti-c/issues/4) |
| 18 | [[18/Audio] Implement playback state, seek epochs and gapless prefetch](https://github.com/shakodev/spoti-c/issues/18) | `phase-3-audio` | [#3](https://github.com/shakodev/spoti-c/issues/3), [#13](https://github.com/shakodev/spoti-c/issues/13), [#14](https://github.com/shakodev/spoti-c/issues/14), [#15](https://github.com/shakodev/spoti-c/issues/15), [#16](https://github.com/shakodev/spoti-c/issues/16), [#17](https://github.com/shakodev/spoti-c/issues/17) |
| 19 | [[19/Audio] Add a headless player and enforce the 8 MB playback budget](https://github.com/shakodev/spoti-c/issues/19) | `phase-3-audio` | [#4](https://github.com/shakodev/spoti-c/issues/4), [#18](https://github.com/shakodev/spoti-c/issues/18) |
| 20 | [[20/Protocol] Implement modern Spotify Connect Dealer and connect-state control](https://github.com/shakodev/spoti-c/issues/20) | `phase-2-protocol` | [#5](https://github.com/shakodev/spoti-c/issues/5), [#8](https://github.com/shakodev/spoti-c/issues/8), [#11](https://github.com/shakodev/spoti-c/issues/11), [#18](https://github.com/shakodev/spoti-c/issues/18), [#19](https://github.com/shakodev/spoti-c/issues/19) |
| 21 | [[21/API] Complete REA-backed Home, Library, Playlist and catalog contracts](https://github.com/shakodev/spoti-c/issues/21) | `phase-4-api` | [#5](https://github.com/shakodev/spoti-c/issues/5), [#8](https://github.com/shakodev/spoti-c/issues/8), [#11](https://github.com/shakodev/spoti-c/issues/11) |
| 22 | [[22/API] Implement bounded Home, Library and Playlist page models](https://github.com/shakodev/spoti-c/issues/22) | `phase-4-api` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#8](https://github.com/shakodev/spoti-c/issues/8), [#11](https://github.com/shakodev/spoti-c/issues/11), [#21](https://github.com/shakodev/spoti-c/issues/21) |
| 23 | [[23/API] Implement typed Search and Album/Artist catalog views](https://github.com/shakodev/spoti-c/issues/23) | `phase-4-api` | [#8](https://github.com/shakodev/spoti-c/issues/8), [#11](https://github.com/shakodev/spoti-c/issues/11), [#21](https://github.com/shakodev/spoti-c/issues/21), [#22](https://github.com/shakodev/spoti-c/issues/22) |
| 24 | [[24/API] Implement synchronized lyrics fetch and a bounded line timeline](https://github.com/shakodev/spoti-c/issues/24) | `phase-4-api` | [#8](https://github.com/shakodev/spoti-c/issues/8), [#11](https://github.com/shakodev/spoti-c/issues/11), [#18](https://github.com/shakodev/spoti-c/issues/18), [#21](https://github.com/shakodev/spoti-c/issues/21), [#22](https://github.com/shakodev/spoti-c/issues/22) |
| 25 | [[25/UI] Build the Win32 window and event-driven rendering scheduler](https://github.com/shakodev/spoti-c/issues/25) | `phase-5-ui-engine` | [#3](https://github.com/shakodev/spoti-c/issues/3), [#4](https://github.com/shakodev/spoti-c/issues/4), [#18](https://github.com/shakodev/spoti-c/issues/18) |
| 26 | [[26/UI] Implement the DX11 instanced renderer and clipped shape shader](https://github.com/shakodev/spoti-c/issues/26) | `phase-5-ui-engine` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#25](https://github.com/shakodev/spoti-c/issues/25) |
| 27 | [[27/UI] Implement DirectWrite shaping and a bounded Clay font atlas](https://github.com/shakodev/spoti-c/issues/27) | `phase-5-ui-engine` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#26](https://github.com/shakodev/spoti-c/issues/26) |
| 28 | [[28/UI] Add asynchronous artwork decoding and a capped texture LRU](https://github.com/shakodev/spoti-c/issues/28) | `phase-5-ui-engine` | [#3](https://github.com/shakodev/spoti-c/issues/3), [#8](https://github.com/shakodev/spoti-c/issues/8), [#22](https://github.com/shakodev/spoti-c/issues/22), [#26](https://github.com/shakodev/spoti-c/issues/26) |
| 29 | [[29/UI] Build Clay widgets, text input and virtualized list primitives](https://github.com/shakodev/spoti-c/issues/29) | `phase-5-ui-engine` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#25](https://github.com/shakodev/spoti-c/issues/25), [#26](https://github.com/shakodev/spoti-c/issues/26), [#27](https://github.com/shakodev/spoti-c/issues/27) |
| 30 | [[30/App] Compose the responsive three-pane shell, Home and Library navigation](https://github.com/shakodev/spoti-c/issues/30) | `phase-6-app` | [#18](https://github.com/shakodev/spoti-c/issues/18), [#22](https://github.com/shakodev/spoti-c/issues/22), [#25](https://github.com/shakodev/spoti-c/issues/25), [#28](https://github.com/shakodev/spoti-c/issues/28), [#29](https://github.com/shakodev/spoti-c/issues/29) |
| 31 | [[31/App] Implement the virtualized Playlist and Tracklist screen with instant filtering](https://github.com/shakodev/spoti-c/issues/31) | `phase-6-app` | [#18](https://github.com/shakodev/spoti-c/issues/18), [#22](https://github.com/shakodev/spoti-c/issues/22), [#29](https://github.com/shakodev/spoti-c/issues/29), [#30](https://github.com/shakodev/spoti-c/issues/30) |
| 32 | [[32/App] Build Search, Album, Artist and synchronized Lyrics screens](https://github.com/shakodev/spoti-c/issues/32) | `phase-6-app` | [#18](https://github.com/shakodev/spoti-c/issues/18), [#23](https://github.com/shakodev/spoti-c/issues/23), [#24](https://github.com/shakodev/spoti-c/issues/24), [#28](https://github.com/shakodev/spoti-c/issues/28), [#29](https://github.com/shakodev/spoti-c/issues/29), [#30](https://github.com/shakodev/spoti-c/issues/30), [#31](https://github.com/shakodev/spoti-c/issues/31) |
| 33 | [[33/App] Integrate SMTC, media keys and Windows playback metadata](https://github.com/shakodev/spoti-c/issues/33) | `phase-6-app` | [#18](https://github.com/shakodev/spoti-c/issues/18), [#25](https://github.com/shakodev/spoti-c/issues/25), [#30](https://github.com/shakodev/spoti-c/issues/30) |
| 34 | [[34/App] Add versioned binary state cache for instant offline startup](https://github.com/shakodev/spoti-c/issues/34) | `phase-6-app` | [#1](https://github.com/shakodev/spoti-c/issues/1), [#3](https://github.com/shakodev/spoti-c/issues/3), [#10](https://github.com/shakodev/spoti-c/issues/10), [#22](https://github.com/shakodev/spoti-c/issues/22), [#30](https://github.com/shakodev/spoti-c/issues/30), [#31](https://github.com/shakodev/spoti-c/issues/31), [#32](https://github.com/shakodev/spoti-c/issues/32) |
| 35 | [[35/App] Enforce final resident RAM, idle CPU, startup and executable benchmarks](https://github.com/shakodev/spoti-c/issues/35) | `phase-6-app` | [#19](https://github.com/shakodev/spoti-c/issues/19), [#20](https://github.com/shakodev/spoti-c/issues/20), [#26](https://github.com/shakodev/spoti-c/issues/26), [#27](https://github.com/shakodev/spoti-c/issues/27), [#28](https://github.com/shakodev/spoti-c/issues/28), [#29](https://github.com/shakodev/spoti-c/issues/29), [#30](https://github.com/shakodev/spoti-c/issues/30), [#31](https://github.com/shakodev/spoti-c/issues/31), [#32](https://github.com/shakodev/spoti-c/issues/32), [#33](https://github.com/shakodev/spoti-c/issues/33), [#34](https://github.com/shakodev/spoti-c/issues/34) |
<!-- BACKLOG_TABLE_END -->

The dependency table is the adjacency representation of the complete DAG. `dependencies` in `backlog.json` is machine-readable and validated for cycles/order. The audio critical path is 01–08 foundations → 09 AP → 10 OAuth → 11 tokens → 13 media source → 14 ranges → 15 decrypt → 16 decoder, joined with 17 sink → 18 player → 19 headless; 12 Mercury precedes 13 for compatibility fixtures. API contract work 21 gates 22–24; UI 25–29 combines with those services in 30–34; 35 closes the release gate.

## Scope completion and outstanding evidence

The original planning deliverable created issues rather than implementations.
Its evidence gaps included XPUI request assembly/base queries, account/client-ID
and private-service compatibility, Dealer runtime acceptance, real WASAPI/device
behavior, WebP capability, decoder budgets and measured hardware thresholds.
Later module investigations are linked in the current-status section above;
their evidence and limitations supersede this initial gap list. Every new
internal API implementation still starts with a reviewed REA-backed spec, and
deterministic tests alone do not establish present live-service compatibility.

Preserve observed versus inferred versus unknown facts in future specs. Never close a feature issue because only an error stub exists, or close a benchmark because its scenario was skipped. If an official endpoint is unavailable, keep that capability visibly blocked with its dependency and evidence rather than silently swapping a historical URI/hash.

Primary platform references consulted: [Spotify PKCE](https://developer.spotify.com/documentation/web-api/tutorials/code-pkce-flow), [CNG raw secret derivation](https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptderivekey), [SMTC GetForWindow](https://learn.microsoft.com/en-us/windows/win32/api/systemmediatransportcontrolsinterop/nf-systemmediatransportcontrolsinterop-isystemmediatransportcontrolsinterop-getforwindow).
