# Shell, Home and Library verification (#42)

**Historical receipt.** The later [completion report](shell-home-library-completion.md)
supersedes the incomplete status and unavailable-reference statements below.

2026-10-20. Isolated worktree `.cache/issue-42`, branch `feat/issue-42`,
based on main `7a1dcb9` and the #41 dependency `719688c`. The user approved
integrating the main worktree's existing live host and read-only account/GPU
acceptance. The original worktree was preserved. **Issue #42 remains incomplete:
reference comparison and several reference-dependent shell details are open.**

## Implemented behavior

The client has a global navigation header, collapsible Library rail, larger
Home shelf cards, and virtualized Library list/grid views. Loaded-page search
and entity filters retain original keys and immutable page ownership. History
restores query, filter, grid and scroll state. Keyboard selection has a visible
focus treatment; long names truncate at UTF-8 scalar boundaries.

Normal startup now installs the existing live service after the first useful
present. State, live and artwork workers share the bounded core arena and join
before their dependencies are released. The imported token-lifetime,
background-thread classification and metadata-status fixes are documented in
[the integration specification](../specs/live-host-integration.md). Unrelated
audio/cache/stall changes from the main worktree were excluded.

REA evidence was recorded before implementation in
[the shell evidence](../specs/shell-home-library-evidence.md) and
[the live artwork specification](../specs/live-collection-artwork.md).
The current native descriptor supplies `picture_size` target labels and URLs.
The parser preserves one exact supplied candidate, preferring HTTPS `small`
within eight candidates, then the first HTTPS candidate. This is a native
bounded admission policy, not an assertion of Spotify's ranking algorithm.
No new endpoint, URL rewrite or inferred image dimension was introduced.

Home and Library now retain that source through their immutable projections.
Visible images enter the existing asynchronous fetch/decode/cache path;
offscreen cancellation, stale generations, terminal failures and deliberate
placeholders remain bounded. A GPU capture found that Clay emitted a background
rectangle after the custom image, hiding uploaded artwork. Browse and catalog
cards now let the custom renderer own that background. The pixel assertion
remains in place and verifies that uploaded artwork is actually visible.

## Debug / ASan

MSVC 19.51 x64, C17, `/W4 /WX`, `CMAKE_BUILD_TYPE=Debug`, `ENABLE_ASAN=ON`.
The separately approved renderer, parity, caption and message visual options
were enabled; live account and physical-audio CTest options remained off.

```text
cmake --build build --clean-first
cmake --build build
ctest --test-dir build --output-on-failure
74/74 passed, 101.10 seconds
```

[Full final CTest log](shell-home-library/asan-ctest.txt). The expected-failure
ASan arena probes pass their harnesses; there were no unexpected sanitizer
errors. The clean build prevents copied source timestamps from leaving old
worker/token objects in the worktree.

The shell cancellation test had a race: navigation could consume the artwork
completion event before the test waited. It now waits for worker quiescence
and still checks cancellation, no GPU allocation and handle balance. Both shell
tests passed three consecutive repetitions before the final full run.

Coverage includes bounded protobuf/image metadata, source lifetime after
wire-buffer reuse, shared state/live/artwork worker ownership and teardown,
loopback HTTP retirement, token bounds, original keys after filtering,
pagination/history, navigation/logout retirement, stale handles, negative
cache limits, offscreen cancellation and keyboard/long/RTL names.

`sizeof(App)` remains 245,224 bytes within the shared 256-KiB App/media budget.
Browse admits at most 15 images, reserving the sixteenth renderer slot for
fonts. Artwork staging remains 768 KiB, compressed input 64 KiB, original edge
512 pixels and decoded edge 256 pixels. The guard checks 2,000 warm layouts,
dense scrolling, 600 route transitions and 100 warm layouts for each capture.
Unchanged visibility does not schedule recurring wakes.

## Native synthetic capture matrix

The real DX11 WARP renderer and DirectWrite fonts produced **36 captures**:
Home, Library list and Library grid at 400/1100/1800 DIP and 96/120/144/192 DPI,
each 900 DIP high. The injected 4x2 PNG, one corrupt source and missing
metadata exercise ready/error/placeholder output. Pixel coverage detects
overpaint, and each capture records dimensions, UTC and a pixel SHA-256.

The [capture index](shell-home-library/native-fixtures/index.json) and its
adjacent PNG/JSON files are unresampled native output. Representative narrow
Home and default Library list/grid captures were inspected for artwork,
clipping, glyphs and RTL labels. These are explicitly synthetic fixtures.
They establish rendering mechanics, not live data or Spotify pixel parity.

## Authorized live account and GPU acceptance

The dedicated probe restored the existing protected session in memory, read
Home/rootlist/playlist metadata, and fetched exact supplied image URLs.
It did not launch login/logout, save credentials, submit playback commands,
open an audio device or mutate Library. Its temporary playlist index was
removed. The probe contains no audio execution path.

- Home used the explicit personalized-Home refusal fallback, showing the
  account's playlists in one application-owned shelf.
- Library returned 16 rootlist rows: four supplied HTTPS image fields,
  zero opaque image fields and twelve missing image fields.
- The sampled selected image fetched 10,623 bytes and decoded to 64x64 with
  balanced resources. Other sampled variants exceeded the existing cap;
  the cap was preserved. This does not establish dimensions or success for
  every URL labelled `small`.
- A playlist returned 64 rows out of 101; all 64 metadata records hydrated,
  and the complete 101-record temporary index was accepted.

See [the read-only probe](shell-home-library/live-account-probe.txt) and
[candidate measurements](shell-home-library/live-account-variants.txt).
These native acceptance logs supplement the retained REA evidence; they are
not presented as REA captures.

`spotic-live-probe --capture` also rendered the actual retained pages through
production App, fonts, HTTPS worker, decoder and DX11 WARP at 1100x900/96 DPI.
All three captures settled and cleanup balanced:

| Live initial viewport | Visible covers | Ready images | Failed/pending | Missing metadata |
| --- | ---: | ---: | ---: | ---: |
| Home fallback | 4 | 0 | 0 / 0 | 4 |
| Library list | 6 | 0 | 0 / 0 | 6 |
| Library grid | 8 | 1 | 0 / 0 | 7 |

The supplied image is visibly rendered in the Library grid. The initial Home
and list rows have no image metadata, so their entity-icon placeholders are
expected. Home visibly states its fallback; the Library limitation is retained
in the existing message transcript. Account pixels stay in ignored
`build/live-captures/`; only status/counts/hashes are published in
[the capture log](shell-home-library/live-account-capture.txt). These are native
client-area renders, not automated interaction with the standalone window.

## Release

The clean MSVC Release build completed with `/W4 /WX`, ASan off and no compiler
warnings. Verification:

```text
cmake --build build-rel --clean-first
ctest --test-dir build-rel --output-on-failure -E '^bench_'
60/60 functional tests passed, 33.26 seconds
ctest --test-dir build-rel --output-on-failure -R '^(bench_dependency_audit|bench_idle)$'
dependency audit passed; bench_idle FAILED its resident-memory limit
```

[Functional CTest log](shell-home-library/release-ctest.txt);
[audit/idle CTest log](shell-home-library/release-idle-ctest.txt).
The [dependency audit](shell-home-library/release-audit.json) passed 408 C
translation units, pinned dependency identities/licenses and six executables,
with no forbidden C++/ASan/runtime-shader-compiler imports. `spotic.exe` is
**1,124,352 bytes**, SHA-256
`4b861c29db242f73dad8577f159d0ee09abcb269e2013c24f21f14b74dd53475`.

The unchanged native gate used the visible offline fixture with fake audio,
on NVIDIA GeForce RTX 2070 / Windows build 26200. During each ten-second
idle/paused interval it measured **0.000000000% machine CPU**, no additional
renders/presents/timeouts, an infinite wait, and only the explicitly induced
ending diagnostic wake. No input contaminated the sample; shutdown balanced.
However, peak resident memory was **67,592,192 bytes idle** and
**71,200,768 bytes paused**, both above the strict **20,000,000-byte** limit.
[Raw measurements](shell-home-library/bench-idle.jsonl).

This is a real gate failure. It is consistent with the previously failing
resident gate in [#35's report](release-gate/README.md), but these runs are not
a controlled attribution of the difference. The fixture excludes live service
startup, so it does not establish live-service memory/CPU. The #35 thresholds,
full release benchmark and cold/live requirements were not weakened or replaced.

## Remaining acceptance gaps and discrepancies

| Area | Current status |
| --- | --- |
| Reference access | Native computer-use inventory fails because its named pipe is unavailable; there is no running Spotify process or supplied CDP endpoint. REA native UI capture is unavailable on Windows. This is a tooling/runtime gap, not missing approval. [Receipt](shell-home-library/native-access.txt). |
| Sidebar/profile | Navigation and collapse work, but there is no persistent populated Library sidebar or reference account/profile menu. Runtime measurements and behavior are still needed. |
| Geometry and states | Header/rail widths, breakpoint, density and minimum height are documented native choices. Reference loading/empty/offline/signed-out/error/hover/focus visuals and resize transitions have not been measured and matched. |
| Required comparisons | Native synthetic matrix and three real-data native renders exist. Actual pinned-reference/native side-by-side captures across three widths and four scales are still absent. |
| Account capabilities | Personalized Home remains unavailable; the fallback and playlist-only Library scope are explicit. Saved albums/artists/folder navigation are not claimed. |
| Image coverage/quality | Missing metadata stays a placeholder. Opaque image/mosaic conversion is not implemented. `small` avoids the sampled compressed-size failure but can look soft on larger/high-DPI cards; the 256-pixel decode cap remains. |
| Keyboard/RTL | Deterministic navigation and shaped RTL labels are covered. Live keyboard interaction and reference typography are unverified; the shell remains left-to-right. |
| Standalone live window | Native offscreen rendering and host lifecycle tests passed. User-visible standalone-window navigation, restart restoration and reference interaction remain unaccepted. |
| Performance | The unchanged native idle gate fails RAM despite passing CPU/quiescence. The #35 full release gate, including controlled cold startup and live playback acceptance, is not replaced by these checks. |

The PR stays draft and #42 stays open until the reference-dependent behavior
and comparison matrix are completed. No fixture result is used to claim
reference parity or unsupported service capabilities.

## PR #46 review follow-up (2026-10-20)

The isolated review starts from `1631da4`; the original worktree is preserved.
Two reauthentication regressions were reproduced before fixing the UI delivery
path. A failed replacement login left the previous account marked signed in,
including its displayed playing track. A successful replacement retained the
previous player's generation, rejecting snapshots from the new player when its
generation restarted at one. Accepted auth completions now retire the old UI
session before publishing success or failure. A stale auth serial still leaves
the newer session intact. These checks use invalid OAuth configuration or a
synthetic completed delivery; no account, browser login or audio device is used.

The comparison-tool fixes from PR #45 are also included: reject output paths
that alias source images/metadata (including hardlinks), read and validate both
captures before exporting, count alpha-only changes, and display alpha deltas
in the difference image. All seven evidence-tool tests pass.

Both strict `/W4 /WX` builds pass with zero warnings. The final Debug/ASan suite
passes **72/72 tests** (125.43 s); Release passes **69/69 functional tests**
(45.89 s) and the dependency audit. Renderer/parity WARP options are enabled;
caption and native-message-dialog options are off, explaining the difference
from the earlier 74-test run. Nine deterministic GPU tests pass in each build.
The audit finds 414 C translation units, six executables and zero failures.
`spotic.exe` remains **1,124,352 bytes**. Raw results:
[ASan](shell-home-library/pr46-asan-ctest.txt),
[Release](shell-home-library/pr46-release-ctest.txt),
[audit test](shell-home-library/pr46-audit-ctest.txt),
[audit record](shell-home-library/pr46-release-audit.json).

No live account, physical audio or performance workload was repeated during
review. The reference-dependent shell behavior and comparison matrix remain
incomplete, and the earlier RAM gate failure remains open. The PR stays draft;
passing correctness tests does not establish issue #42 or release acceptance.
