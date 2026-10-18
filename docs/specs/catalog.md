# Catalog contracts and document identity

Shared URL/method/headers, provenance and error/fallback rules: `pathfinder.md`.
The source and local captures are pinned to desktop 1.3.4.258.

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
schemas. An independent `queryAlbumTracks` continuation was observed, but its
error fixture remains missing. The primary `getAlbum` contract has observed
positive, NotFound and exhausted-page fixtures and passes the evidence gate.
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
The capture proves a successful primary response; it does not prove every
availability, prerelease or continuation variant.

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

Search/category, pagination and error fixture coverage is still incomplete.
Only the primary Album entry currently passes the proof gate. Other entries
remain unavailable. The stored
sanitized positives are genuine observed response structures, not invented SDL.
