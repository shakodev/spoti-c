# spoti-c

A native Windows Spotify client written in C17, using Win32, DX11, Clay,
DirectWrite, miniaudio/WASAPI and bounded protocol/API services.

As reviewed on 2026-10-19, issues #1–#31 are implemented. The repository includes
core memory/worker primitives, authenticated transport and playback modules,
a headless player, API page models, rendering, fonts, artwork and widgets.
`spotic.exe` presents a responsive Home/Library shell, a virtualized Playlist
with instant filtering, keyboard navigation and a persistent playback bar.
SMTC, additional screens, startup state cache
and final release gates remain in
[issues #32–#35](https://github.com/shakodev/spoti-c/issues?q=is%3Aissue+is%3Aopen).
Closed module issues do not establish final application usability or every
live-service and hardware compatibility claim. See each module's specification
for its tested scope and remaining limits.

The project targets <20 MB resident RAM, <2 MB executable size, <30 ms cold
startup and displayed 0.0% idle/playback CPU. These are acceptance targets;
the complete GUI release gates remain open. See [the roadmap](docs/ROADMAP.md)
and [engineering instructions](AGENTS.md).

## Build and test

Use an x64 Visual Studio Developer PowerShell with MSVC's C toolchain, Windows
SDK (including `fxc`), CMake, Ninja, AddressSanitizer and Python 3.12 installed.
The Visual Studio C++ Build Tools workload supplies the C compiler; CMake enables
only C, and application code does not use the C++ runtime.

Install the pinned Python build tools once in a local environment:

```powershell
py -3.12 -m venv .cache/proto-tools
& .cache/proto-tools/Scripts/python.exe -m pip install -r tools/proto-requirements.txt -r tools/crypto-requirements.txt
```

Then configure Debug with strict warnings and ASan, build and run deterministic
tests:

```powershell
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DPython3_EXECUTABLE="$PWD/.cache/proto-tools/Scripts/python.exe"
cmake --build build
ctest --test-dir build --output-on-failure
```

The reviewed Debug suite contains 52 tests. Project targets use `/W4 /WX` on
MSVC or `-Wall -Wextra -Werror` with compatible drivers. Protobuf generation
verifies pinned inputs; [the codec specification](docs/specs/protobuf.md)
documents generation and the optional C/runtime audit. Vendored sources and
checked-in fixtures permit builds without `ref/` or network access after tool
installation.

Use a separate Release build for timing and performance tests:

```powershell
cmake -B build-rel -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_ASAN=OFF -DPython3_EXECUTABLE="$PWD/.cache/proto-tools/Scripts/python.exe"
cmake --build build-rel
ctest --test-dir build-rel --output-on-failure
```

Live account, physical audio-device and optional GPU checks require explicit
`ENABLE_LIVE_*` or `ENABLE_RENDERER_GPU_TESTS` CMake options. They are absent
from the default suite. A skipped live test is not evidence of compatibility.

## Executables and source

- `build/spotic.exe`: Win32/DX11 application shell; the default host is signed
  out and has no configured live collection/account transport. Run
  `build/spotic.exe --demo` for synthetic Home/Library pages and real playback
  through the offline fake device. `--demo-wasapi` explicitly enables audible
  synthetic audio on a physical device. See [shell usage and verification](docs/verification/app-shell.md)
  for service-host integration and current limits.
  Startup restores a bounded offline shell from `%LOCALAPPDATA%/spoti-c/shell.state`
  before service initialization; cached items never authorize playback. See
  [cache validation and startup measurements](docs/verification/state-cache.md).
  Select a playlist in the demo's Home or Library to browse 10,005 synthetic
  tracks, filter title/artist/album, change order and play selected rows.
  See [playlist verification and timings](docs/verification/playlist-screen.md).
- `build/spotic-headless.exe`: bounded playback harness with deterministic
  offline fixtures and separate live opt-ins; see [usage and validation](docs/headless.md).
- `src/core/`, `src/protocol/`, `src/audio/`, `src/api/`, `src/ui/`: implemented
  modules with their own C headers and explicit ownership/error outcomes.
- `proto/`: 38 pinned Spotify schemas and bounded nanopb options, described by
  `proto/manifest.json`.
- `vendor/`: pinned C dependencies, source digests and upstream licenses.
- `docs/specs/`: module contracts, REA evidence and verification limits.
- `tests/`: deterministic tests, sanitized fixtures and explicitly gated live
  or Release benchmark targets.

## Planning and evidence

[ROADMAP.md](docs/ROADMAP.md) separates the current implementation status from
the original architecture plan. `docs/backlog.json` and `docs/issue-map.json`
preserve the original 35-issue specification and number mapping; they are not
live completion trackers. GitHub issues and the implemented module specifications
describe subsequent work and any changes to the initial plan.

Historical source observations remain in
[transport-and-playback.md](docs/specs/transport-and-playback.md) and
[xpui-static.md](docs/specs/xpui-static.md). Their notices point to the later
authentication, playback, API and native CDN contracts that supersede initial
implementation proposals and unresolved gates. Dated evidence is retained with
its original artifact identity; it is not a promise of present server behavior.

`ref/` and `.cache/` contain optional local reference clones, extracted Spotify
artifacts and large inspection records. They are ignored by Git, along with
build products and credential material. Do not commit credentials, signed CDN
URLs, private traffic captures or local tool configuration. New internal API
behavior requires REA-backed evidence before implementation, as specified in
[AGENTS.md](AGENTS.md).
