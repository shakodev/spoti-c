# Collection artwork adapter (#42)

Recorded before implementation on 2026-10-20. This is a bounded native adapter
for already-retained response metadata. It adds no endpoint, request header,
GraphQL document, image URL rewrite, authentication or account observation.

The existing [Home and Library contract](home-library-playlist.md) identifies
Home item wrappers as `content.data`, with independently typed playlist, track,
album and artist objects. Its native Library contract and the sanitized
`native-library-observed-positive.json` fixture retain `entityInfo.imageUri`.
[Typed catalog views](catalog-views.md) documents the Track/Album/Artist image
mapping and missing-dimension fallback policy. [Artwork evidence](ui-artwork.md)
retains REA parent
`ev_bba7e6b7d6f36242f405f48598c56ae0c0a645255ed5539ac127ebdebff1aff3`
for the playlist, album and artist source inspection. It explicitly establishes
playlist `images.items[].sources[]`, track `albumOfTrack.coverArt.sources[]`,
album `coverArt.sources[]` and artist `visuals.avatarImage.sources[]`.
Those recorded observations are the authority for this adapter; no new
inspection or live compatibility is claimed.

The reader accepts the typed `CollectionItem.source` already owned by a
retained collection page. It copies at most eight source URLs and their
dimensions into caller-owned fixed storage. It validates intermediate JSON
objects/arrays, supported Home discriminants, unsigned dimensions, UTF-8,
embedded NUL, URL capacity and HTTPS URL syntax. Missing/null dimensions are
represented as a paired zero/zero value, not invented dimensions. A partially
known size is rejected. The decoder remains responsible for bounded source
preflight and output size. Native Library `imageUri` is used only when it is
already a valid HTTPS URL. `spotify:image:`, other schemes and absent artwork
remain unavailable; converting these requires separate REA evidence.

More than eight source candidates is an explicit capacity result, including
flattened playlist variants. Missing/null optional metadata or unsupported
entity/URI kinds produces an unavailable result. Wrong JSON types, inconsistent
Home discriminants, invalid dimensions and malformed HTTPS URLs produce an
invalid result. Failure clears the output count and identity, so partial
results cannot accidentally reach the artwork service.

The image cache identity is SHA-1 of the number of copied variants and each
variant's exact UTF-8 URL bytes, prefixed by its byte length and unchanged
width/height using a fixed byte order. It is a cache identity, not a security
claim. All admitted variants contribute, so changing a non-first selected
variant cannot reuse stale artwork. Requested output size and navigation
generation remain separate `ArtworkId` fields. Hashing uses Windows CNG only
at the preparation boundary.

`app_collection_image` rejects layout/render calls, builds the existing
`AppEntity`, copies the bounded source description and delegates to `app_image`.
It retains no JSON/URL pointer. Root screen preparation owns visible-set
enumeration, service wake integration, terminal-failure suppression and
navigation lifetime; rendering never invokes this reader or fetches images.
The reader adds no persistent bytes to `App` and allocates no application heap.

Tests use synthetic JSON and local CNG hashing only. They cover every supported
source path, independent owned URL lifetime, Unicode, unsupported schemes,
missing/null data, malformed types, dimensions, URL limits, candidate overflow
and cache identity changes. They perform no network, account or GPU work and
do not establish that the live rootlist fallback supplies cover metadata.
