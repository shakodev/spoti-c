# Shell, Home and Library contract (#42)

Recorded before implementation, 2026-10-20. The pinned reference and shared
tokens are in [spotify-desktop-parity.md](spotify-desktop-parity.md).
The installed package remains Spotify 1.3.5.3058; historical `ref/xpui` is
1.3.4.258. Neither static package is a runtime screenshot.

The focused REA investigation for this issue is retained in
[shell-home-library-evidence.md](shell-home-library-evidence.md). Its selected
JavaScript parent is
`ev_591ff2bc2f44e77aeb27efdf4c6d23eca9c048aa0a27f332ecffbc9cbc1c05ed`:
nine files, 241,222 bytes, no parse failures, with partial application and
semantic coverage. In particular, `dwp-top-bar.js` is a route/entity header;
it does not establish the placement of global navigation.

## Native composition and explicit unknowns

The implementation will compose a client-area header, independent Library
panel, scrollable main panel and persistent player boundary. Windows continues
to own the nonclient caption. The #40 message transcript and Read messages
action remain reachable by keyboard. The #38 volume control and its compact
player arrangement remain intact.

The following are native layout choices to be compared with reference captures,
not claims of measured Spotify breakpoints:

- Eight-DIP outer/panel gaps and panel corners; 56-DIP header.
- Expanded Library width 280 DIP; 64-DIP collapsed rail. The existing
  760-DIP compact threshold remains a native rule.
- Interactive windows have a 420 by 560 DIP minimum outer size so Library
  controls remain reachable above the compact player. Restoring window bounds
  honors that minimum within the current monitor work area. This is a native
  constraint, not a measured reference minimum.
- Main content uses 12-DIP inset. Card columns derive from available width,
  with a 160-DIP target and 12-DIP gaps, bounded by the fixed visible-image
  and control budgets.
- Home uses titled shelves with larger square artwork, separate navigation
  targets and a play affordance only for entities the existing player can
  actually play. Playlist/album activation navigates; it must not pretend to
  start an unsupported context queue.
- Library offers list/grid, loaded-page text search and entity filters.
  Filtering never changes the transport cursor or claims account-wide search.
  It keeps an index projection over the retained immutable page.
- Selection, hover, pressed and keyboard focus have separate presentation.
  Directional navigation follows visual columns. RTL names request the existing
  DirectWrite RTL shaping role; the shell itself retains left-to-right layout.
  Original names remain UTF-8; display truncation ends at a scalar boundary.
  Search folds ASCII case and matches other UTF-8 bytes exactly.

Runtime reference shell widths, resize transitions, profile behavior,
scrollbar appearance, populated account variants and tooltip timing are still
unknown. The screenshot matrix must retain these gaps rather than treating
native fixtures as live parity evidence.

## Artwork and ownership

Image source readers use metadata already supplied by immutable pages. They
do not invent URLs, API endpoints, image dimensions or account capabilities.
Home source paths are checked against the existing catalog/artwork evidence;
Library accepts a supplied HTTPS image URL and leaves opaque image URIs
unresolved. The bounded reader contract is documented separately in
[collection-artwork.md](collection-artwork.md).

Layout records only visible item identities in fixed storage. A subsequent
preparation step copies source URLs into the existing asynchronous artwork
service. A change of visible set requests one state wake; unchanged warm
frames cause no wake or allocation. Completion sends a worker event to the
window's existing blocking wait. Fetch, decode and upload stay outside layout
and rendering.

The artwork service must cancel work that leaves the visible set, retain
terminal failures until explicit retry/navigation, reject stale generations
and texture handles, and use centered cover cropping with rounded corners.
See [artwork-visibility.md](artwork-visibility.md). Missing metadata, failed
decode, pending download and a ready texture remain distinguishable outcomes.
The placeholder is an intentional entity icon; usable supplied images must
replace it after bounded asynchronous completion.

Pages remain owned until replaced or navigation retires them. UI state copies
only bounded names/identities or indices; no weak JSON pointers survive page
release. Search/filter/grid and scroll restoration must not mutate page rows.
Logout clears account-derived UI state.

## Capability boundary and live prerequisite

Committed main at `7a1dcb9` has no installed live GUI service: normal startup
uses empty `AppServices`, and only `--demo` installs fixture services.
The main worktree contains an uncommitted live host with additional token,
thread and audio dependencies. Those user changes are preserved separately
unless inclusion is authorized.

That live host's personalized Home request can return 403, with an explicit
real-rootlist "Your playlists" fallback. Its Library scope is playlists only,
and its current projections omit artwork metadata. These are service gaps,
not reasons to fill the UI with fixture recommendations or unsupported
albums/artists. Account-side validation remains blocked until the prerequisite
is integrated and account/GPU acceptance is explicitly opted in.

## Verification plan

Deterministic tests cover geometry and caption/player separation at
96/120/144/192 DPI; narrow/default/wide widths; Home density; Library
filter/grid/list, keyboard/RTL/long names; back/cancellation and page ownership;
artwork success/failure/stale/offscreen handling; and allocation-free warm
frames. Full Debug/ASan CTest and a strict Release build are required.

Opted-in captures must pair actual reference and native client images at each
scale and width without resampling. Record account/service state, theme,
language, viewport, DPI, zoom, artifact hashes and UTC timestamp. Fixture
captures validate rendering mechanics only. Live Home/Library behavior and
all reference discrepancies require separate records. The #35 release
performance gate remains unchanged.
