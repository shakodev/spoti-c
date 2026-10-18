# Synthetic Connect fixtures

Generate with `python tools/connect_fixtures.py` (Python standard library only).
These contain invented devices, command IDs and track IDs, with no account data,
tokens or recorded Spotify traffic. Protobuf tags follow the pinned repository
schemas; Python encodes them independently of nanopb. JSON, base64 and gzip are
also generated independently of the production C implementation.

The fixtures cover a paused transfer at 120 ms, an upcoming queue, a current
queue item selected by `is_playing_queue`, finite inline context pages, rejected
pagination/queue overflow, all supported playback controls, cluster/volume/logout,
stored/dynamic DEFLATE, optional gzip headers/FHCRC, and an expansion bomb.
Tests mutate CRC/trailer/header/truncation cases in memory. Native WinHTTP tests
use loopback peers and fake audio; the default suite needs no account or device.

See [the Connect specification](../../../docs/specs/connect.md) for evidence,
limits, lifecycle and the separate opt-in two-device acceptance command.
