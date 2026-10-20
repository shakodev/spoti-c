# Shell/Home/Library completion receipt (#42)

2026-10-20/21. This supersedes the incomplete acceptance status in
[the earlier report](shell-home-library.md), whose historical measurements are
preserved. Work stayed in `.cache/issue-42`; the dirty original checkout was
preserved. Main's player-bar PR #47 (`5ccfdd4`) was merged into this branch before
the final shell changes, retaining its metadata, transport, queue and clocks.

## Implementation and ownership

The global Home/Search group is centered between navigation and account
controls. Search is a filled pill with a leading glyph and muted body label;
Home is a filled circle. The account menu uses left-aligned body rows, traps
keyboard traversal, closes on Escape/outside activation and restores avatar
focus. Settings and authentication use existing services; absent account,
profile, history, support and private-session services show explicit reasons.

The Library sidebar independently retains an immutable projection of up to 16
consumed rootlist rows. Two finite projections share the existing 64-KiB
collection-model reservation; they occupy 55,992 bytes together with the
collection service. They do not retain a JSON reservation, so main-page
pagination remains possible. Navigation preserves populated sidebar rows while
cancelling replacements; logout/account replacement retires them and stale
deliveries cannot repopulate the old session. Search explicitly covers the
loaded page. Scrolling, selection, keyboard navigation and collapse use original
row identities.

Wide browsing supports six columns and 18 visible artwork records, including a
partial third row. Separate playlist play buttons wait for the existing playable
index and finite queue. Card-body activation remains navigation. Route changes,
refresh, query/sort changes, logout and failures cancel pending play. No playlist
identity is treated as a track ID, and no endpoint was added.

The decoded texture budget stays 4 MiB with 32 cache entries and 768 KiB staging.
Up to 18 browse, 10 sidebar and one persistent player image coexist; browse decode
requests cap at 224 pixels and shrink further only at extreme DPI to reserve
sidebar/player bytes. Forty renderer descriptors accommodate those resources
and fonts. `App` is 258,968 bytes; App plus SMTC is 261,176/262,144 bytes. Replaceable
word-measure entries were reduced to 192; the rollover/no-pending-font and warm
heap guards remain enabled.

## Reference and native evidence

[Spec-first evidence](../specs/shell-runtime-completion.md) records REA's exact
installed PE identity, resource inventory and selected xpui analysis. The
provider's native UI utility reports that it requires macOS; Windows Computer
Use supplies the separately attributed runtime observations. Earlier statements
that no reference process or computer-use surface was available are historical.

The installed Spotify 1.3.5.3058 reference has executable SHA-256
`4704c2b7cd76878ab36656f09989cfa132a02f9711d7a8707750a8ff68722136`.
Twenty-seven reference captures cover Home, Library list and Library grid at
three window widths under 100/125/150% Windows display scaling. Each saved JPEG
hash was verified. Sky exposes logical-pixel images; these are not described as
unresampled physical pixel readbacks. The metadata-only
[reference index](shell-home-library/runtime-reference-index.json) records exact
sizes, OS scale, timestamps, hashes and provenance. Raw account pixels stay in
ignored `.cache/reference42/`.

The selected 1920x1080 display offers standard 100/125/150/175% scales. The user
explicitly chose to keep standard scaling when 200% required custom scaling.
No custom scaling or sign-out occurred; Windows was verified restored to its
original 100%. There is no 200% reference capture and no claim of four-scale
reference pixel parity. Native 192-DPI coverage remains separate.

The native live probe supports `--capture-matrix` (36 exact physical renders at
400/1100/1800 DIP and 96/120/144/192 DPI) and `--capture-reference` (27 renders at
the actually observed reference viewports). Both use genuine retained account
pages and an independent real rootlist projection, production HTTPS/decoder/cache,
DirectWrite and DX11 WARP. The readback allocation is capped at 25,920,000 bytes,
outside frames, securely cleared and freed after each capture. JSON records
geometry, source counts, terminal/pending states and hashes; the command receipt
records balanced cleanup for every capture.

Local paired previews normalize native DPI only for visual comparison. Original
physical RGBA/lossless PNGs remain unchanged. JPEG capture, logical coordinates,
different service content and caption ownership preclude a meaningful physical
pixel pass/fail score; no such score is manufactured.

All 63 native captures settled with zero pending main/sidebar requests and 63
balanced cleanups. The retained sidebar had 14 playlist rows, the main Library
page had 16 source rows including unsupported entries, and Home had one real
fallback shelf. Supplied sources produced one visible successful cover in wider
Home/grid views and two terminal cover failures in wider Library grids; missing
metadata remains a placeholder. List views initially show rows without usable
supplied sources. These counts are reported rather than treating placeholders
as downloaded artwork. [Native metadata](shell-home-library/runtime-native-index.json),
[paired-preview hashes](shell-home-library/runtime-comparisons.json) and the
[live command receipt](shell-home-library/completion-live-probe.txt) are public;
account pixels are not.

The complete 27-pair matrix was reviewed as contact sheets, with full-size Home
default/100%, Library list narrow/150% and Library grid wide/100% samples. Header,
panel and player boundaries remain separated, and pagination remains reachable.
The differences listed below remain visible. Native geometry ends at the actual
whole-pixel render extent: fractional physical sizes use `MulDiv` rounding, so
817 DIP at 120 DPI becomes 1021 pixels/816.8 DIP. Only preview copies normalize
this to the requested reference logical viewport.

## Standalone Release interaction

With the saved account and approved desktop/GPU access, the actual Release
window loaded real sidebar rows and supplied artwork, the explicit Home fallback
and Library list/grid. Typing a loaded-page query filtered the grid. Keyboard
navigation selected Settings from the account menu; sidebar rows remained
populated. Alt+Left restored the grid/query, and collapse/expand retained rows.
Restart restored the saved Library route. Account activation displayed its
unavailable reason and returned focus to the avatar.

Under the existing short-playback approval, the Library playlist hover button
loaded a 101-track index and began playback. Track title/artist/artwork and the
progress clock updated. Playback was paused at nine seconds and the app closed.
This check used the existing local playlist/queue path, not fixture audio or a
new service operation. Private screenshots remain local.

## Discrepancies and capability boundaries

| Area | Observed reference and native result |
| --- | --- |
| Shell geometry | Reference uses a 64-DIP header, 8-DIP panel gaps/radii and 88-DIP persistent player boundary. Native layout now asserts those boundaries and preserves the separate native caption. |
| Caption and compact layout | Spotify integrates caption controls into its black global header. Native Win32 owns a separate caption. Native accepts 400-DIP width and uses a 120-DIP compact bar below 760 DIP; the observed reference window was at least 800 DIP. These are documented native limits. |
| Sidebar width/resize | The saved reference sidebar was 412 DIP and shrank at narrow widths. Native uses 280 DIP or a 64-DIP collapsed rail, with explicit collapse and no arbitrary drag-width storage. This difference is visible in paired captures. |
| Home content | Reference includes personalized announcements, shortcuts and shelves. The existing 403 fallback shows real playlists in one explicitly labelled shelf. Personalized recommendations are not fabricated. |
| Library capabilities | Reference includes folders, saved releases, artists, podcasts, local/downloaded entries, owner/date-added/last-played columns and sorting. Native retains playlist-only rootlist scope, finite-page search/filter and list/grid selection. Unsupported rows, absent metadata columns, global search/sort and other services are not claimed. |
| Density/type | Native caps wide layout at six columns to preserve its visible-image budgets; reference expanded Library can show more columns. Fonts use Segoe UI/DirectWrite fallback, not Spotify's proprietary face. Names are UTF-8 safe and shaped RTL runs are tested; the shell itself remains left-to-right. |
| Artwork | Usable supplied HTTPS sources render; rows without supplied metadata retain explicit entity placeholders. No opaque/mosaic URL conversion was invented. Small supplied source images can look soft on large/high-DPI cards. |
| Menus and notifications | Native supports Settings/auth and reports absent services. Family-plan/update/activity/notification integrations and reference private-session behavior remain service gaps. Read messages preserves complete diagnostics instead of clipping them into the reference chrome. |
| Other states | Deterministic loading/empty/offline/signed-out/error, hover/selection/focus and stale-account tests cover native state ownership. The signed-in reference was not logged out or placed offline to manufacture state captures. Shared observed surfaces/colors are used; full reference state equivalence is not claimed. |
| 200% reference | Excluded under the user's explicit standard-scaling constraint; native 200% DPI remains tested. |
| Performance gate | #35's strict release thresholds remain intact. Historical RAM failures are retained; correctness and capture success do not certify that separate release gate. |

## Final checks

The final source was built in the Visual Studio 18 x64 environment with strict
`/W4 /WX`. Every changed C module is below 500 lines (the largest is 480).

| Check | Final result and receipt |
| --- | --- |
| `cmake --build build`; `ctest --test-dir build --output-on-failure` | **77/77 passed**, 162.73 s, Debug `ENABLE_ASAN=ON`. Includes GPU, parity, real caption/input and native message checks. [Raw receipt](shell-home-library/completion-asan-ctest.txt). |
| `cmake --build build-rel`; `cmake --build build-rel --target spotic-live-probe` | Both strict Release builds passed without compiler warnings. |
| `ctest --test-dir build-rel --output-on-failure -E '^bench_'` | **74/74 passed**, 77.95 s, including the same opted-in desktop/GPU checks. [Raw receipt](shell-home-library/completion-release-ctest.txt). The three extra Debug cases exercise expected ASan arena poisoning failures. |
| Release dependency audit | **Passed**, 436 C translation units and six audited executables; no forbidden runtime imports or vendor identity failures. [Audit](shell-home-library/completion-release-audit.json). |
| Synthetic matrix | **36** final Debug/ASan WARP captures: Home/list/grid, three widths, four DPI scales. Every RGBA hash verified, exported lossless PNGs without resampling, including long/RTL text, a corrupt image and absent metadata. [Index](shell-home-library/native-fixtures/index.json). |
| `build-rel/spotic-live-probe.exe --capture-matrix --capture-reference` | **Passed**, real account authorization/rootlist/Home fallback/Library, 63 settled native captures and balanced cleanups. All raw hashes verified and 27 private paired previews generated. The read-only diagnostic also accepted 64 playlist metadata rows and the complete 101-record disk index, then removed its temporary index. |
| `ctest --test-dir build-rel --output-on-failure -R '^(bench_dependency_audit\|bench_idle)$'` | Audit passed; **idle gate failed RAM**, 56,492,032 bytes idle and 61,366,272 bytes paused against 20,000,000. Average machine CPU was 0.0000%/0.0129%, below 0.05%; both phases had no input contamination, no extra renders and only the induced diagnostic wake. Shutdown balanced. [CTest](shell-home-library/completion-release-idle-ctest.txt), [raw samples](shell-home-library/completion-bench-idle.jsonl). #35's thresholds and harness were not weakened. |

The Release `spotic.exe` is **1,192,960 bytes**, SHA-256
`09dfb0217b1848ffd4fb8d6cd3ad818a57b7f205b3b4f99a04283a1161010d52`.
This closes the shell implementation and evidence work with the documented
service/native limitations and the user's standard-scaling constraint. It does
not certify #35's separate all-metrics release gate.

The caption capture harness now activates and verifies its own visible window
through the existing explicitly opted-in real-input path before testing inactive
caption states. This fixes background CTest foreground-permission failures;
active/inactive, button hover/press, restore/maximize/minimize and close checks
remain enabled. The text-cache test uses `APP_MEASURES` instead of the previous
hard-coded capacity; rollover, no-pending-font and 1,000 warm-frame heap guards
are unchanged. Recovery/pagination tests cover short windows, message previews,
retry, loading, empty/no-match content and keyboard scroll visibility at 96/192 DPI.
