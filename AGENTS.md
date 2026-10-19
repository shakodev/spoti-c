# AGENTS.md — Custom Pure-C Spotify Client

## Project Overview

An ultra-lightweight, native Windows Spotify desktop client written in pure C (C17/C23).

Target metrics: < 20 MB resident RAM, 0.0% CPU when idle or playing audio with no mouse movement, < 2 MB executable size, instant (< 30 ms) cold startup.

## Technical Stack (Strict Pure C)

- **Language:** C17/C23 (`clang-cl` or `MSVC` via CMake + Ninja). No C++ files or standard library (`stdc++`/`msvcprt`) allowed.

- **Windowing & GPU:** Native Win32 API + DirectX 11 (instanced quad/rounded-rect/text pixel shader).

- **UI Layout:** `clay.h` (nicbarker/clay) — single static memory arena, zero heap allocations per frame.

- **Audio Output:** `miniaudio.h` (WASAPI backend, shared low-latency mode).

- **Audio Decoding:** `stb_vorbis.c` (Ogg Vorbis stream decoding).

- **Spotify Streaming Protocol:** Pure C port modeled on `ref/cspot` and `ref/librespot`:

  - Protobufs: `nanopb` (static structs where possible).

  - Crypto: Windows CNG (`bcrypt.lib`) for Diffie-Hellman, SHA, and AES-128-CTR + pure C `shannon.c` stream cipher.

- **JSON & Internal APIs:** `yyjson` + native Windows `WinHTTP` (zero external TLS/HTTP DLL bloat).

- **Reverse Engineering:** Use the `rea` MCP server (`morluto/rea`) to inspect `ref/xpui/` (extracted Spotify frontend) and `Spotify.exe` when documenting internal PathFinder GraphQL queries and client behavior.

## Repository Layout

- `src/core/` — Arena allocators, ring buffers, thread pool, logging, OS primitives.

- `src/protocol/` — ApResolve, Diffie-Hellman handshake, Shannon cipher, Mercury frames, Nanopb messages, AES audio chunk fetcher.

- `src/audio/` — Vorbis stream decoder, PCM ring buffer, `miniaudio` WASAPI sink, gapless track transitions.

- `src/api/` — WinHTTP async client, Token/ClientToken manager, PathFinder GraphQL queries, `yyjson` parsers.

- `src/ui/` — Win32 message loop, DX11 instanced renderer, DirectWrite/stb_truetype font atlas, `clay.h` components and screens.

- `vendor/` — Single-header C dependencies (`clay.h`, `miniaudio.h`, `stb_vorbis.c`, `yyjson.c/.h`, `nanopb`, `shannon.c/.h`).

- `proto/` — `.proto` definitions and `nanopb` `.options` files.

- `ref/` — (Gitignored) Local clones of `cspot`, `librespot`, and extracted `xpui.spa` for reference and REA inspection.

- `docs/specs/` — REA evidence and protocol/API Markdown specifications written before implementing features.

- `tests/` — Standalone C unit and integration test binaries.

## Non-Negotiable Engineering & Memory Rules

1. **Zero Per-Frame Heap Allocations:** Never call `malloc`/`free` inside the UI layout or DX11 render loop. Use `Clay_Arena` and pre-allocated frame scratch arenas that reset via pointer bump at `EndFrame()`.

2. **0% Idle CPU (Event-Driven Rendering):** Never run an uncapped `while(true)` game loop. Use `MsgWaitForMultipleObjectsEx` in the Win32 loop. Only wake and render a frame on:

    - User input (mouse/keyboard/window resize)

    - Network/audio state change event (`SetEvent` from worker threads)

    - A 1 Hz timer tick (when playing) to advance the playback progress bar, or 60 Hz strictly during active scroll/hover transitions.

3. **Strict Sanitizer & Compiler Cleanliness:**

    - All code must compile with `-Wall -Wextra -Werror` (or `/W4 /WX` on MSVC).

    - Debug tests must run with AddressSanitizer (`-fsanitize=address`) enabled and pass with zero leaks or buffer overruns.

4. **Spec First for Reverse-Engineered Features:** Before coding any Spotify Web/GraphQL API endpoint, use REA on `ref/xpui` to verify the exact request headers, query hashes, and JSON shapes, and document them in `docs/specs/`.

## Build & Verification Commands

- Configure Debug (with ASan): `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON`

- Build: `cmake --build build`

- Run Tests: `ctest --test-dir build --output-on-failure`

- Configure Release (Size/Speed optimized): `cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release`

## Issue Execution Workflow

When assigned a GitHub Issue (`#N`):

1. Read the issue requirements with `gh issue view N`.

2. Inspect relevant files in `ref/` or query REA if specified.

3. Implement the changes in small, modular C files (< 500 lines per file).

4. Write or update the corresponding unit test in `tests/` and run `ctest --test-dir build --output-on-failure`.

5. Only consider the task complete when builds and ASan tests pass with zero warnings.
