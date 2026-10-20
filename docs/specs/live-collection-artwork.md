# Live collection artwork projection (#42)

Recorded before implementation on 2026-10-20. This preserves supplied metadata
from the authorized live-host integration. It adds no network endpoint,
decoration value, image identifier conversion or account mutation.

## Evidence and scope

The current `playlist_api.proto` descriptor was previously read through REA
from Spotify 1.3.5.3058, DLL SHA-256
`4f8a965aa4492c6cbfa304956ea8428f10538c74dfae5eaadf54be08572a897a`.
The complete original descriptor bytes remain in
[api-contract-native-backends.json](api-contract-native-backends.json).
Its original range is `[30093952,30105174)`, SHA-256
`a6d3a154e30e38d3e7425170b72adf5db2ff23d7d5d364eaecb60f3c33008ee6`,
REA evidence
`ev_5fd01c5c2403dde60be3b7a1a0e2e0fb9c5cb3a3f5cec015036eec6e35bf27b7`.
This exact descriptor, rather than the older librespot schema, establishes:

| Message | Field | Tag | Type |
| --- | --- | --- | --- |
| SelectedListContent | contents | 5 | ListItems |
| ListItems | meta_items | 4 | repeated MetaItem |
| MetaItem | attributes | 2 | ListAttributes |
| ListAttributes | name | 1 | string |
| ListAttributes | picture | 3 | bytes |
| ListAttributes | picture_size | 13 | repeated PictureSize |
| PictureSize | target_name | 1 | string |
| PictureSize | url | 2 | string |

`PictureSize` does not supply numeric width/height. `picture` is opaque bytes;
it is not established as a URL or an image-file identifier usable without a
resolver. No dimensions are inferred from `target_name`.

The read-only decoder
[inspect-live-artwork.cjs](shell-home-library/inspect-live-artwork.cjs)
independently verifies the retained descriptor hash and emits the selected
field definitions in
[live-artwork-fields.json](shell-home-library/live-artwork-fields.json).
Attempting to run that reader through REA process capture returned
`capability_unavailable`: the Windows PTY adapter cannot verify descendant
cleanup. The failure receipt is retained. The reader subsequently ran with
Node through the ordinary shell; this supplemental decoding is **not** a new
REA/native observation and does not replace the earlier verified byte read.

The exact inventoried installed-package files `6746.js` and `7189.js` were
analyzed through REA before implementation:
parent `ev_b9242a980a174dc0c3dc9be0a6e4f0832e95ca48aa8284ae3444e2ed7e60d3c3`;
summary `ev_79a9a18d7d3befcb1bacc6159a9cc3681c85a9b7e82899f6f330913bb219af43`.
Two files, 49,049 bytes, zero parse failures; 271 unselected files and 6,043
semantic unknowns remain. The retained summary and hash-bound byte snippets
show `imageUri` supplied to an imported resolver and image candidates mapped
through `imageUriOrUrl`. The resolver implementation is not selected, so these
consumers alone do not establish any `spotify:image:` to HTTPS conversion. The
package is parsed as inert text; no Spotify process or account request ran
in this investigation.

The subsequently inventoried current V8 snapshot source
`xpui-modules.js` (8,378,367 bytes, SHA-256
`991cc95a5fdb9104aba8aa95c3de511c577e914be155447ff12cabc05715e296`)
contains those imported implementations. The inert AST extraction reader and
[module hash/span manifest](shell-home-library/image-resolver-extraction.json)
retain exact member boundaries. REA parsed only extracted modules `43575` and
`71646`, parent
`ev_e4c8f569bbfc180af7ea7039d420e67379a178c504dd3172407692b3dfc9d095`,
summary
`ev_56fbaedbea64e7decaccbaaae4354f786b728fa56e487090a1696232eaab3052`;
the [receipt](shell-home-library/rea-image-resolver-summary.json) records
2,961 bytes, zero parse failures and 247 unresolved semantic relationships.
Module `71646` contains an explicit `spotify:image:` to
`https://i.scdn.co/image/` mapping and a mosaic mapping; `43575` selects
projection identifiers through an imported dimension lookup. That lookup was
not selected, and no conversion or projection rewrite is implemented here.
These static observations do not establish which resolver ran for this account.

## Authorized read-only acceptance and revised candidate policy

The ordinary native account probe in
[live-account-variants.txt](../verification/shell-home-library/live-account-variants.txt)
is runtime acceptance evidence, **not a REA capture**. Its first sampled Library
row supplied four exact HTTPS URLs:

| Supplied target label | URL bytes | Fetch with 64 KiB cap | Decode |
| --- | ---: | --- | --- |
| `default` | 95 | `ART_TOO_LARGE`, no retained bytes | Not attempted |
| `small` | 91 | OK, 10,623 bytes | OK, 64 x 64 |
| `large` | 91 | `ART_TOO_LARGE`, no retained bytes | Not attempted |
| `xlarge` | 93 | `ART_TOO_LARGE`, no retained bytes | Not attempted |

The probe requested a 128-pixel decoded output. The observed 64 x 64 result is
specific to this image; `small` is not interpreted as a numeric dimension.
No account identifiers, URLs, response bodies or image bytes are retained in
the log. Home used the explicit rootlist fallback, and Library returned 16
rows with four usable HTTPS artwork rows. This sample motivates a bounded
native admission policy: prefer the first exact supplied HTTPS candidate
whose case-sensitive `target_name` equals `small`, retaining the first HTTPS
candidate when there is no such candidate. This is not a claim about the
Spotify client's ranking algorithm, all playlists or all future responses.

## Native subset

The wire reader copies one URL/URI string per rootlist row into fixed owned
storage (1,024 bytes including terminator; 64 rows). It examines at most eight
`picture_size` candidates. Selection priority is first HTTPS `small`, then
first other HTTPS, then first nonempty opaque/non-HTTPS value. Candidates of
equal priority preserve wire order. An opaque candidate labeled `small`
never outranks a supplied HTTPS URL. Missing, empty, mixed-case and unknown
labels have ordinary fallback priority. Labels have a native 63-byte bound
plus terminator, using temporary stack storage only. The persistent row
representation and existing compressed/decode/upload limits are unchanged.
The reader does not derive sizes, rewrite schemes, concatenate an image host,
or interpret `picture` bytes.

Duplicate singular target/URL/name/metadata attributes, invalid UTF-8, embedded NUL,
over-capacity strings/candidate lists and malformed wire fields fail decoding.
Missing URL, empty candidates and absent artwork remain valid metadata.
Well-formed unknown fields are skipped. The existing item/meta row alignment
and immutable page rules remain in force.

The Library projection preserves the selected value as `entityInfo.imageUri`.
The application-owned fallback Home and shelf projections preserve it as
`content.data.images.items[0].sources[0].url`, with omitted dimensions.
These are native projections into the existing typed model, not claims that
the backing rootlist replied with PathFinder JSON. JSON serialization owns
the bytes before the worker response buffer/list can be reused.

The [collection artwork reader](collection-artwork.md) admits supplied HTTPS
URLs, validates their syntax, and uses paired zero dimensions when unknown.
Opaque Spotify image/mosaic URIs remain present in the immutable page but
unavailable to this fetcher. `picture` bytes alone remain a placeholder.
Successful personalized Home responses already pass through without rewriting
their supplied image metadata.

Deterministic tests must cover actual protobuf tag nesting, candidate priority,
missing/opaque metadata, malformed/oversized/duplicate fields, fixed bounds,
both JSON projections, and page lifetime after overwriting the wire/list
storage. Updated-policy end-to-end acceptance remains separate from the
single-candidate probe above. Service-wide coverage and visual parity are
not established by these static observations and synthetic tests.
