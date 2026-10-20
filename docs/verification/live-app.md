# Live application integration (#30)

This report was imported from the original working tree on 2026-10-20 as
historical provenance. Its results describe that earlier source state, including
headless changes not included in issue #42. They do not establish acceptance of
the isolated integration; see [the import specification](../specs/live-host-integration.md).
The current #42 probe is read-only and has no audio execution path; the
`SPOTIC_PROBE_AUDIO` behavior described below applies only to that historical
source. Current results are in [the #42 report](shell-home-library.md).

The standalone application now installs a background `AppServices` owner after
its first useful frame. The worker owns browser OAuth, encrypted AP credential
establishment, DPAPI persistence/restoration, scoped Login5/clienttoken requests,
Home, rootlist playlists, paginated playlist contents and extended track metadata.
Search, Album, Artist and available Lyrics route through their existing models.
Live playback uses the existing headless network/decode owners and WASAPI; demo
mode remains explicitly selected with `--demo` or `--demo-wasapi`.

Launch `build-rel/spotic.exe`, click Sign in and complete the system browser
flow. Credential values and callback codes are never displayed or logged by the
host. The default OAuth identity and loopback redirect come from the pinned
librespot reference. `SPOTIC_OAUTH_CLIENT_ID`, `SPOTIC_OAUTH_REDIRECT_URI` and
`SPOTIC_OAUTH_SCOPES` override them for a registered developer client. Private
service acceptance is a separate compatibility requirement. `SPOTIC_COUNTRY`
and `SPOTIC_TIME_ZONE` select the playback market and Home IANA zone (current
defaults: NL and Europe/Amsterdam).

The private application directory contains DPAPI `credentials.dat`, the bounded
encrypted audio cache in `audio/`, and a temporary immutable `playlist.index`.
Logout cancels old authentication/route jobs, stops playback and removes the
credential/index files. Normal shutdown retains credentials for restart. The
playlist adapter preserves opaque revisions and rejects changes during paging;
it has an explicit 100,000-record disk limit and uses the existing 10,000-entry
filter windows and finite 64-track player queues. Library is explicitly a
playlist view; saved-album/artist aggregation and nested folder navigation are
not represented as completed native Library IPC support.

## Deterministic checks

- `app_live_host`: missing saved session, invalid login configuration without
  opening a browser, cancellation of a queued login, stale authentication serial
  suppression, DPAPI logout deletion, failed playlist loading-state retirement
  and worker/arena teardown.
- `app_live_wire`: required native protobuf fields, truncation, 64-row bounds,
  one-byte mutation sweep, URL escaping, typed Library projections, Home request
  escaping and integration literal, track identity/metadata/duplicate occurrences.
- `app_live_http`: actual loopback HTTP completion, response body overflow,
  cancellation admission, absence of credentials on uncredentialed requests and
  handle retirement.

Debug and Release builds use strict warnings. Existing shell/navigation/player
tests remain required. The Debug ASan suite contains 56 tests; final build and
test results below are updated after the last integration changes.

Final verification on 2026-10-19: the complete Debug build and all 56 ASan
tests passed. The final account handle-retirement adjustment and playlist error
state have corresponding live-host regression coverage. A clean Release build of
`spotic` passed with zero compiler warnings in `build-live/`; this alternate
directory avoids the running older `build-rel/spotic.exe` file lock.

## Live acceptance

On 2026-10-19 the user completed browser login and confirmed the GUI signed in.
The initial content failures exposed client-token expiry bounds (the real grant
lasts 1,216,800 seconds), opaque 24-byte playlist revisions, and Windows service
threads acquiring USER32 queues. These now have bounded handling and regression
coverage. Worker and headless owner threads explicitly register their background
role; actual UI threads retain the blocking-operation guards.

An explicit account probe used the saved DPAPI session and printed only numeric
status, counts and public diagnostics. It accepted:

- Home's real-data playlist fallback and 16 Library rootlist rows, HTTP 200.
- A 101-track playlist, paged in batches of at most 64, including metadata and
  the complete immutable disk index.
- Physical WASAPI playback after explicit user approval: PLAYING, no audio error,
  and at least three seconds of device-consumed track timeline before shutdown.

The playback resolver initially rejected a successful TRACK_V4 provider status
of 200. It now admits the observed 200 as well as the prior default 0, while
rejecting other statuses. The same live probe then passed completely.

Personalized PathFinder Home still returns HTTP 403 with this client/session.
Home visibly reports the restriction and displays the account's actual playlists
under an application-owned shelf. This is not personalized Spotify Home. Library
currently exposes rootlist playlists; saved albums/artists and folder navigation
remain outside that adapter. Search/Album/Artist/Lyrics are connected to their
existing services but their live compatibility has not been accepted here.

Final Release GUI content/navigation, playback controls, automatic restart
restoration and logout still require user-assisted verification because the
native computer-use helper is unavailable. Deterministic tests cover these
lifecycles without using the user's account; they do not establish live GUI
acceptance. No account identifiers, tokens, track names or response bodies are
checked in. The optional `spotic-live-probe` target is excluded from the default
build and CTest; physical output additionally requires `SPOTIC_PROBE_AUDIO=1`.
