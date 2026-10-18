# Catalog contracts and document identity

Shared URL/method/headers, provenance and error/fallback rules: `pathfinder.md`.
Documents are pinned to desktop1.3.4.258. Follow-up captures use desktop1.3.5.3058
to replay those same documents; they do not claim the newer default routes use
unchanged hashes. Each capture and source keeps its own identity.

## Album

Snapshot module 2410 declares `getAlbum` and `queryAlbumTracks`, each with hash
`6a74b456cd1735c9193d9e8ec8cc5184cad7ce13572210315229db3975964361`.
Module 36920's primary Album call supplies `{uri,locale,offset:0,limit:50}` and
the source-local `spotify-app-version: 896000000` override. `uri` is a Spotify
album URI; `locale` can be empty. A null locale is not observed at this call.

The captured primary response has `data.albumUnion.__typename: Album`, plus
`name`, `uri`, `artists`, `coverArt.sources`, `tracksV2.{items,totalCount}`,
`discs`, `releases`, `copyright`, `date`, `playability`, and nullable prerelease
fields. `tracksV2.items[].track` contains track URI/name, duration, artists,
content rating, relinking information and playability; keep its wrapper and
`uid`. Unknown/non-Album discriminants must not be parsed as Album.

The same hash in two document constructors does not prove two identical reply
schemas. Independent `queryAlbumTracks` requests now establish offset0 success,
offset2 continuation and NotFound; `getAlbum` separately has observed positive,
NotFound and exhausted-page fixtures. Both pass their individual evidence gates.
Module 76146 identifies `GenericError`, `NotFound` and `RestrictedContent` as
Album errors; only NotFound is an observed service error in this fixture set.
Neither `queryAlbumMerch` nor mini-player `getAlbumNameAndTracks` is the primary
Album contract. Body caps are local policies; reject rather than truncate.

## Artist

The main Artist route imports recovered module 41231, whose
`queryArtistOverview` hash is
`9f8134ef565e78621f1e1793555bd6633c5ac144ae0f89604ed3ae3f80b3c8e6`.
Its variables are `{uri,locale,preReleaseV2}`. The observed locale is empty and
`preReleaseV2` is boolean true; the source reads the boolean from remote config.
It supplies the same local app-version override as Album.

The primary response is rooted at `data.artistUnion.__typename: Artist`.
Treat nested wrappers/discography collections and availability independently.
The captures prove a successful primary response and a NotFound union for a
missing artist. This overview request is not paginated: module 41231 uses one
query with only `uri`, `locale`, `preReleaseV2`. Prefetch repeats the same query;
discography pagination belongs to separate operations. Its manifest records this
source-backed nonpagination instead of inventing offset variables. Availability
and prerelease variants remain explicit response unions rather than assumed
successful artist data.

`ref/xpui/3308.js` contains another `queryArtistOverview` constructor with hash
`1ac33ddab5d39a3a9c27802774e6d78b9405cc188c6f75aed007df2a32737c72`
and `{uri,locale:null}`. Those are different call sites. Do not reuse that
hash/nullability in the main Artist route merely because its operation name is
the same. The original `xpui-static.md` observations remain valid for that call.

## Search

`xpui-routes-search.js` declares legacy `searchDesktop`
(`1148393611bbc58e84e47aed35ecc731275df9f9eb660956962e352dd3631d89`)
and `searchTracks`
(`b02683192a98dde7966b5e6655a79eeb62713eab703eda9902c932818dd52751`).
Category requests use `searchTerm`, `offset`, `limit`; the adapter merges feature
flags/defaults and checks GraphQL errors before consuming `data.searchV2`.
The page-size guard is 100. Check `pagingInfo.nextOffset` for exhaustion; do not
assume one collection's count describes the entire search response.

The experiment-controlled list adapter instead requests `searchTopResultsList`
(`12d940b3f7381506a82ff5382eb59b0ed65118656e6bcdfbe4cebc528f75d29d`)
using `query`, `limit`, `offset`, `numberOfTopResults`, include flags, nullable
`isPrefix`, `sectionFilters[]`, and `clientFeatures[]`. Suggestions are a third
operation (`searchSuggestions`) and are not evidence for either primary query.
Store operation identity and its actual variables together. Never pair a hash
from one branch with another branch's variables.

Independent SearchTracks offset0/offset2 replies and a numeric limit101 GraphQL
error are retained. The main List branch has its own positive, offset2 and
limit101 error fixtures. Boolean-limit HTTP400 responses are explicitly labeled
rejected inputs, not accepted variable shapes. Main List counts are checked at
`data.searchV2.topResultsV2.itemsV2`; the chip-order array is not a result page.
Legacy SearchDesktop now has independent offset0, offset2 and numeric limit101
GraphQL-error observations. Its own descriptor uses `searchTerm` plus
`offset`, `limit`, `numberOfTopResults`, `includeAudiobooks`,
`includeArtistHasConcertsField`, `includePreReleases`,
`includeAlbumPreReleases`, `includeAuthors`, `includeEpisodeContentRatingsV2`,
nullable boolean `isPrefix`, `sectionFilters[]`, and optional `clientFeatures[]`.
The source forwards this variable object directly; the list adapter's `query`
rename does not apply. `clientFeatures` is omitted when the source's suggestion
state helper returns undefined. All three operations pass their separate gates.
The stored sanitized positives are genuine observed response structures, not
invented SDL. Replay provenance and the desktop-version distinction are recorded
in `pathfinder.md`.
