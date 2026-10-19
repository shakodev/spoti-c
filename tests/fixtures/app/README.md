# Synthetic application-shell pages

These small, hand-authored fixtures exercise the existing collection parser
shapes documented in `docs/specs/home-library-playlist.md`. Titles, identifiers,
pagination and unavailable items are synthetic; these are not captured account
responses or evidence of live endpoint compatibility. Home/Library continuation
and shelf pages feed the same bounded CollectionService used by the app host.
Track ID 1 selects the existing synthetic encrypted audio fixture in `--demo`.

Search, Search continuation, Album/AlbumTracks, Artist and Lyrics bodies extend
this demo through the existing CatalogService/LyricsService parsers. Their
shapes follow `docs/specs/catalog-views.md` and `lyrics-timeline.md`; metadata,
base62 IDs, 5-second durations and lyric timings are synthetic. The deliberately
long album title and multilingual strings exercise bounded display text. These
files add no observed service schema or endpoint evidence.
