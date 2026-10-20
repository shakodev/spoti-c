# Live application host (#30)

The standalone GUI must bind `AppServices` after its first useful frame. One
bounded worker owns OAuth, AP credential establishment, HTTP, tokens, API models
and playlist index writes. UI callbacks enqueue work and never wait for I/O.
Pages cross the boundary through release/acquire publication; the UI consumes
all deliveries, including stale generations. Cancellation must retire HTTP
before reusing response buffers. Logout cancels pending login and requests,
stops playback, deletes DPAPI credentials and clears account pages.

An accepted reauthentication completion retires any previous UI session before
publishing its result. Failure leaves the UI signed out; success installs a
fresh session whose player snapshots may restart their generation sequence.
Superseded auth serials must not clear a newer session.

Protocol authorities are the existing `oauth-credentials.md`, `auth-tokens.md`,
`pathfinder.md`, `home-library-playlist.md`, `native-library-playlist.md` and
`playback-resolution.md`. No Web API replacement or guessed GraphQL hash is used.
REA reinspection of the five #30 XPUI files on 2026-10-19 retained parent
`ev_5839348d823afc8ec75e8d565b1fd1ef9f3673176efd90a822c32d36812ad237`,
artifact `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.
Five files parsed, no parse failures; 266 sources remain outside selection and
semantic coverage is truncated. This establishes static identities, not live
service acceptance. Native capture reinspection retained parent
`ev_3b517bbb90ae79bbfb75c58b2c3ef72a64f9e9ed378ade77061d513fc56a4ee7`.
The inert extracted snapshot modules 53045 and 2903 were also inspected by REA
(parent `ev_c95410c9cd47a3616237f187219c9d7628c00e41dbed219b96bd5152f9ee4101`):
53045 exports `mg` from `uF.IntegrationDesktop`; 2903 maps that enum to the
literal `INTEGRATION_DESKTOP`. Both parsed without failures; dependencies and
runtime behavior remain outside that static observation. The request uses this
literal, not an inferred integration name.

Playlist presentation metadata uses the existing evidenced extended-metadata
endpoint and TRACK_V4 envelope. Up to eight repeated EntityRequest messages are
encoded per batch with the existing nanopb encoder. Responses validate entity URI,
Any type, track GID, UTF-8 and bounded fields before copying title/artist/album
and zigzag duration into immutable disk records. Unavailable rows retain explicit
missing metadata; the playback resolver still enforces market/file restrictions.

The host implements its own bounded read-only Library adapter from the evidenced
rootlist backing service; it does not claim to implement Spotify's local IPC
aggregate. GET `/playlist/v2/user/{escaped-username}/rootlist` and
`/playlist/v2/playlist/{validated-base62-id}` use `from`, `length`, and
`decorate=revision,attributes,length,owner,capabilities,status_code`.
Authorization and client-token are scoped to the resolved spclient origin;
Accept is application/protobuf. Decode SelectedListContent revision(1),
length(2), attributes(3), contents(5); ListAttributes name(1); ListItems
pos(1), truncated(2), Item(3); Item required URI(1). Preserve opaque revisions
as hex, validate required fields, bounds, lengths and cursor progress. Never
interpret these bytes as a local YourLibraryService response. Project the
rootlist to the existing typed UI model with an explicit playlist-only scope.
Playlist records are written in bounded batches into a private disk index;
missing metadata remains explicit and never disables a valid track identity.

Browser login defaults to the public desktop OAuth client and loopback `/login`
used by the pinned librespot reference; environment overrides permit a registered
developer client/redirect/scopes. OAuth issuance alone does not authorize a
session: encrypted AP login must succeed, then save its reusable credentials.
Restart uses DPAPI credentials. Playback uses the existing live headless owner
and physical WASAPI. No fixture audio or account data is substituted in live mode.

Observed desktop client-token grants expire after 1,216,800 seconds and refresh
after 1,209,600 seconds. Client-token admission therefore has a separate finite
31-day cap; Login5 bearer admission retains the one-day cap. Revisions are opaque
bytes: the observed native revision is 24 bytes, beyond the media file-ID helper's
20-byte maximum. The adapter encodes revisions independently and rejects any
revision that cannot fit the existing disk-index contract.

When personalized Home returns 403, the application explicitly displays the
restriction and presents a shelf of actual rootlist playlists instead. This
application-owned shelf has URI `spoti-c:playlists`; its paging advances through
raw rootlist positions, including skipped folder entries. It does not claim a
successful personalized Home response. No catalog hash or endpoint is replaced.

Verification must exercise cancellation, stale auth serials, parser bounds,
malformed/truncated protobuf, page retirement, restart/logout, worker shutdown
and existing GUI/ASan regressions. Actual browser/account and audible playback
acceptance is reported separately and cannot be inferred from offline tests.

The account-dependent, status-only probe on 2026-10-19 observed HTTP 200 with
TRACK_V4 array `provider_error_status=200`, followed by entity status 200. The
older resolver admitted only provider status 0 as a local policy, without live
evidence. Admit 0 (the existing omitted/default success fixture) and 200; keep
other provider statuses rejected. No response bodies or account identities are
retained by the diagnostic.
