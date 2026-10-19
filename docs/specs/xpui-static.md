# XPUI static inspection and unresolved contracts

Historical evidence from the initial 2026-10-17 planning pass. Preserve the
artifact identities, literals and coverage limits below; the unresolved-contract
table describes that pass, rather than the current implementation status.
Later REA-backed work is documented in [auth-tokens.md](auth-tokens.md),
[pathfinder.md](pathfinder.md),
[native-library-playlist.md](native-library-playlist.md),
[home-library-playlist.md](home-library-playlist.md), [catalog.md](catalog.md)
and [lyrics.md](lyrics.md). Use those module contracts for implemented requests;
the older literals below must not replace newer evidenced service behavior.

Date: 2026-10-17. Spotify installation manifest version: `1.3.4.258`; `ref/xpui.zip` SHA-256: `19be39b9f8da51a5e766699bc0e7a8bf6dc6f09b1b733a2d8feb4cb2e1ce0608`. REA directory identity: `8fe63d71062b5b05fc69d319c4856d99431b4e5f404f0ffe641ce6c827712728`.

REA 6.5.0 `analyze-javascript-application` analyzed 16 distinct selected entry/shared files across two passes, with zero parse failures. Pass one Evidence: `ev_40a8864feaeed9523967983d24c87a0f7b49671e3921eb187ddef6af5b19344d`; pass two: `ev_bccb24f5aae88349db668a9d993cea3eca35a9a49bc70101fb90bce22d400701`. Complete local evidence is retained under `.cache/rea-xpui-roadmap.json` and `.cache/rea-xpui-auth-roadmap.json`; [xpui-evidence.json](xpui-evidence.json) stores the portable summary, limitations, selected paths, source digests and relevant query literals.

This is inert static analysis. It neither executed Spotify nor observed successful HTTP traffic. Pass one retained static/module facts for `9806.js` but its semantic graph exceeded the remaining result budget. Selection left 261/265 of 271 relevant files outside each pass. Supplemental string/constructor searches scanned all top-level `.js` files; their observations are source inspection, not additional REA semantic coverage.

## What the available files establish

`ref/xpui/index.html` preconnects `api.spotify.com`, `apresolve.spotify.com`, `api-partner.spotify.com`, and `spclient.wg.spotify.com`. This proves host hints, not an endpoint path or HTTP method.

`xpui-snapshot.js` starts with a loader invoking externally supplied `__webpack_modules__[e]`; it does not contain all those module definitions. The dependency graph consequently has unresolved shared modules. `pip-mini-player-snapshot.js` registers `AuthorizationAPI`, `RootlistAPI` and a GraphQL request context; `home-ads-container.js` calls an injected token provider's `getToken()` and formats `${tokenType} ${accessToken}`. `xpui-debug-window-contents.js` reads `accessTokenExpirationTimestampMs`, retry state and expiry. These are concrete consumer contracts, not proof of token acquisition protocol or generic GraphQL headers.

| Surface | Observed contract | Still unresolved before network implementation |
|---|---|---|
| Home | `9806.js` checks `data.home.__typename === HomeResponsePayload`, `sectionContainer.sections.{totalCount,items}`, `greeting.transformedLabel`; `home-v2.js`, `home-subfeed.js`, `home-v2-section.js` supply composition consumers | Base Home operation/hash, variables, full paginated item union, shared request implementation |
| Search | `xpui-routes-search.js` declares `searchDesktop` and category operations; invokes `_request(document, variables, ..., signal)`, checks `errors`, reads `data.searchV2.{albumsV2,artists,tracksV2,playlists,...}` and extension request IDs; page-size guard is 100 | Actual HTTP method/path/serialization/complete required headers, full variable nullability, live acceptance |
| Playlist | `xpui-routes-playlist.js` has `playlistSection`, inline curation and `thisisPlaylistExtension` documents plus shared service consumers | Primary playlist metadata/items operation/hash and revision/pagination wire contract |
| Album | `xpui-routes-album.js` supplies route consumers and `queryAlbumMerch`; mini-player snapshot declares `getAlbumNameAndTracks` and `queryAlbumTrackUris` | Main album-detail document/hash and complete wrapper shape; merch/track-URI documents are not substitutes |
| Artist | `3308.js` calls `queryArtistOverview` with `{uri,locale:null}`, explicitly adds `spotify-app-version: 896000000`, reads `data.artistUnion.__typename === Artist`, `visuals.avatarImage.sources`; artist route declares discography documents | Generic headers and runtime acceptance; the observed app-version override is local to that call |
| Library | Collection songs and your-library route delegate to shared services; mini-player snapshot registers RootlistAPI | Transport, operation/hash, pagination and revision details |
| Lyrics | `xpui-routes-lyrics.js` gets current item and delegates fullscreen rendering to a shared component | Backend URL/method, JSON/protobuf format, timing/rights/error contract; do not assume it is GraphQL |
| clienttoken/Bearer | Provider consumption/expiry fields above; librespot's independent protobuf protocol is documented in transport-and-playback.md | XPUI's exact issuance body, headers, challenge/refresh path, token domains and generic PathFinder request assembly |

The initial plan assigned the auth evidence gap to issue #11 and the remaining
screen contracts to issue #21. Subsequent investigations and their exact scope
are recorded in the linked module specifications above. Future endpoint changes
still require REA evidence, preserved provenance and redacted credentials. No
guessed hashes, copied old internet query catalogs, or treating librespot headers
as verified XPUI headers. Unsupported or unavailable endpoints stay explicit
errors until their gate passes.

## Representative exact document literals

These hashes identify **this artifact**; they are not a live compatibility promise. UTF-8 byte offsets and file SHA-256 are in `xpui-evidence.json`.

| Operation | Hash | Source |
|---|---|---|
| searchDesktop | `1148393611bbc58e84e47aed35ecc731275df9f9eb660956962e352dd3631d89` | `ref/xpui/xpui-routes-search.js` |
| searchTracks | `b02683192a98dde7966b5e6655a79eeb62713eab703eda9902c932818dd52751` | same |
| searchAlbums | `202cb3305e31e5a0767ba7925f28bd728cf8f8b0217e6da43909056071cd70e9` | same |
| searchArtists | `7bf95d754fdbe32c8b161fbbe54d1ae50974900df4dce4c8f1afcbcad153224d` | same |
| searchPlaylists | `d520014e748f9ea44f7707d8df1819867ac1205e8b7f3e28f22fe5fc858921b1` | same |
| queryArtistOverview | `1ac33ddab5d39a3a9c27802774e6d78b9405cc188c6f75aed007df2a32737c72` | `ref/xpui/3308.js` |
| queryArtistDiscographyAlbums | `5e07d323febb57b4a56a42abbf781490e58764aa45feb6e3dc0591564fc56599` | `ref/xpui/xpui-routes-artist.js` |
| playlistSection | `2615df403a9043c1d7d3094fbeb4c9653b07b11a33d8081fbd31f0f7959ff4a1` | `ref/xpui/xpui-routes-playlist.js` |
| getAlbumNameAndTracks | `8628ad33de3267d7bef516c76a746979a5f98891a2c9eaff3dfec828abdcd983` | `ref/xpui/pip-mini-player-snapshot.js` |
| queryAlbumTrackUris | `a2a17981f8439ca1798f56260277d9d7800ec0ca7040053b564e0f975d8aa344` | same |

Search's list adapter builds `query`, `limit`, `offset`, `numberOfTopResults`, several include flags, `isPrefix`, `sectionFilters`, and `clientFeatures`; category calls use `searchTerm`. Do not conflate those variable contracts. UI parsers must handle GraphQL errors alongside partial data, wrapper `__typename`, missing/null fields and pagination exhaustion. These are bounded consumer observations, not a recovered full GraphQL SDL.
