# Lyrics service contract

REA snapshot parent and artifact identity: `pathfinder.md`. Recovered module
2200 constructs this request independently of GraphQL:

`GET https://spclient.wg.spotify.com/color-lyrics/v2/track/{trackId}/image/{image}`

Both path arguments are separately `encodeURIComponent`-encoded. `trackId` is
the parsed Spotify track URI's ID (base62), not the hexadecimal metadata GID.
`image` is the artwork URL, encoded as one path argument. Query fields are
`format=json`, `vocalRemoval` (boolean serialized as a query value), and optional
`clientLanguage` (system language or `all` on the observed feature branches).
The builder's default method is GET and response type is JSON. Default session
market may be appended because this call does not use `.withoutMarket()`.

The standard builder supplies Bearer authorization, `Accept: application/json`,
locale and global desktop platform/app-version headers. This call does not use
`.withJsonContentType()`. No persisted query hash or operationName belongs here.
The bearer must be permitted for this service origin; do not copy the PathFinder
origin grant or treat a client-token as the user bearer.

Module 24063 reads `lyrics.{syncType,lines,previewLines,provider,
providerDisplayName,providerLyricsId,alternatives,capStatus}`, `colors.{text,
highlightText,background}`, and optional `hasVocalRemoval` (defaults false).
`LINE_SYNCED` and `SYLLABLE_SYNCED` count as time-synced. Each line has
`startTimeMs` and `words`. The consumer converts starts to numbers; a line ends
at the next start, and the final end is unbounded. Do not turn these observations
into a promise that every track has lyrics or every timing mode is identical.

HTTP 404 is mapped to `{hasLyrics:false}`; other failures propagate. Preserve
rights/capping/upsell semantics rather than presenting capped preview lines as
the full lyric. Never save original lyric text in a test fixture: locally redact
words and provider/user strings while preserving types, timing and counts.

Locally sanitized requests observed a successful lyric (104 lines) and a 404 with
a non-JSON body; the latter is retained as a status-only projection with null
body, never invented JSON. Numeric timestamp strings and sync discriminants are
preserved; lyric words are redacted.

Availability is another request: module 2200 converts the track ID to a GID and
GETs `/metadata/4/track/{gid}`, then checks `has_lyrics`. That does not establish
a successful lyrics response.

This service is not paginated. Module 2200 constructs one track request without
an offset/cursor; module 24063 consumes the returned lines and preview lines in
one ordinary query, with no next-page request. The manifest records this source
evidence explicitly. The independent success and status-only404 now satisfy the
Lyrics contract's fixture gate. The fixture represents observed LINE_SYNCED and
capStatus NONE; other timing/rights values are not claimed to have been observed.
Their source consumers remain documented above, and unknown values must not be
silently coerced to the observed variant.
