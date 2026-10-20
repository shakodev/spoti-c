# Shell, Home and Library implementation verification (#42)

2026-10-20. Worktree `.cache/issue-42`, branch `feat/issue-42`, based on
main `7a1dcb9` plus the #41 dependency cherry-pick `719688c`.
This is a **partial implementation with acceptance still open**.

## Implemented and checked

The client has a separate global navigation header, expandable Library rail,
larger Home shelf cards, and virtualized Library list/grid views. Loaded-page
search and entity filters retain original item keys and immutable page
ownership. History restores query, filter, grid and scroll state. Long display
names truncate at UTF-8 scalar boundaries without changing entity identities.
Keyboard selection receives the same card focus treatment as pointer focus.

Typed readers copy existing Home and Library HTTPS artwork metadata; no new
Spotify endpoint or image-URI conversion was added. The existing artwork
pipeline now cancels offscreen work, retains failures, crops covers, and wakes
the UI through its event queue. Missing, pending and failed images have distinct
icon treatments. An explicit route retry retires the generation and failures.
The native host opens after the first useful present and joins its worker
before releasing renderer/queue storage.

Reference findings were recorded before implementation in
[the evidence specification](../specs/shell-home-library-evidence.md).
The [implementation contract](../specs/shell-home-library.md) separates
observations from native choices.

### Debug / ASan

Configured with MSVC 19.51 x64, C17, `/W4 /WX`,
`CMAKE_BUILD_TYPE=Debug`, `ENABLE_ASAN=ON`.

```text
cmake --build build
ctest --test-dir build --output-on-failure
59/59 tests passed
```

[Full CTest log](shell-home-library/asan-ctest.txt).
The existing expected-failure ASan arena probes also pass their harnesses.
No unexpected sanitizer failure remains.

Coverage includes 400/1100/1800-DIP layouts at 96/120/144/192 DPI, dense Home
and Library pages, RTL shaping roles, original keys after filtering, pagination,
logout/navigation retirement, retained page replacement, stale results,
worker cancellation/notification lifetime, negative-cache capacity, limited GPU
slot simulation, malformed artwork metadata, and copied-source lifetimes.
These deterministic tests use synthetic pages and injected GPU callbacks.
They do not prove live account behavior or reference pixel parity.

`sizeof(App)` is 245,224 bytes within the existing 256-KiB state budget.
The shared App/media-state compile-time budget still passes. Visible browse
images are capped at 15, reserving one renderer texture slot for the font atlas.
Staging remains 768 KiB and requested decode edges remain capped at 256 pixels.
The heap guard passes 2,000 warm layouts and additional dense scrolling;
600 route transitions keep page residency bounded. Unchanged visibility
does not schedule recurring wakes.

### Release

Release is configured with ASan off and strict warnings. Native performance
targets are compiled for the unchanged dependency/import audit; optional
account, GPU and native performance acceptance is not executed by this report.
The exact results and binary identities are retained alongside this report.

- **18/18 selected Release tests passed**, including the unchanged
  `bench_dependency_audit`: [CTest log](shell-home-library/release-ctest.txt).
- The [audit](shell-home-library/release-audit.json) reports 390 pure-C
  compilation units, six audited executables, all pinned dependencies valid,
  strict warning flags, and no forbidden C++/ASan/compiler runtime imports.
- `spotic.exe`: **1,054,720 bytes**, SHA-256
  `3ac2efd09e9c99923460e99fcc74b876160ec2af7be8205ebe5ab0f538d32bd7`.
  [REA's static PE receipt](../specs/shell-home-library/native-release-pe.json)
  independently records that artifact identity. It does not execute the app.

During worktree setup, the #41 LF attributes arrived after the initial
checkout. Restoring the three yyjson files to their pinned LF bytes resolved
the initial vendor-identity audit failure; no vendor-content change remains.
The #40/#41 font test now warms separate measurement and rendering keys before
asserting allocation-free reuse.

## Acceptance gaps and discrepancies

| Area | Current status |
| --- | --- |
| Live GUI service | Committed main installs no live GUI adapter. The main worktree's uncommitted live host and dependent fixes have not been absorbed into this isolated branch. Normal startup therefore cannot complete account browsing. |
| Actual artwork | Reader/cache/host path is implemented and tested with injected data. Live Home/Library projection currently omits artwork and still requires integration with the live adapter. |
| Home capability | The live host's personalized Home 403 and real-rootlist fallback require live validation. No fixture recommendations or invented endpoint were substituted. |
| Library capability | Filters operate only on loaded supplied entities. They do not establish album/artist account support, which remains playlists-only in the pending live adapter. |
| Sidebar and profile | Sidebar provides navigation/collapse, not a persistent populated Library sidebar. Header has search navigation, Settings and authentication actions, not the reference account/profile menu. |
| Geometry | Header height, expanded/collapsed widths, 760-DIP breakpoint, card density and 560-DIP minimum outer height are documented native choices awaiting measurements. |
| Typography / RTL | Existing DirectWrite roles shape RTL labels. The shell and directional grid remain left-to-right. Full RTL shell parity and exact Spotify typography are unverified. |
| Image quality | The existing 256-pixel decode cap remains; large cards at high DPI may scale beyond native image resolution. |
| Runtime states | Loading/empty/error/offline/signed-out behavior remains functional, but reference state visuals have not been captured and compared. |
| Capture matrix | No reference/native side-by-side captures exist for the 3 sizes × 4 DPI scales. Synthetic geometry checks are not substitutes. |
| Performance | #35 gates and thresholds are unchanged. Native idle/startup/RAM performance has not been rerun under the required separate opt-in. |

Issue #42 must remain open until the live adapter is integrated, the remaining
shell details are implemented from runtime evidence, and the required opt-in
account/GPU/reference acceptance is completed. The account/GPU opt-in request
and the request to include the uncommitted live-host dependency are pending.
