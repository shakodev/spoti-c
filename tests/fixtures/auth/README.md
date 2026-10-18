# Authentication fixtures

These offline fixtures substitute invented tokens, usernames, credentials,
device IDs and challenge contexts. They are schema-based test vectors, not
captured successful Spotify traffic. No personal credentials belong here.

The wire definitions are the repository's pinned librespot protobuf closure.
`docs/specs/auth-tokens.md` records independent official-client evidence and its
limits; the native request-assembly gate passed before endpoint implementation.
`native-evidence.json` retains REA IDs, artifact/range digests and limitations.
Expected request bytes use Python protobuf, independently of production nanopb.
Hashcash answers use Python hashlib. Native Windows tag 10 means Windows Store;
this desktop fixture leaves it false. The x64 fixture models Windows 10 build
26100 and AMD64 architecture, with invented client version and identity.

Regenerate intentionally using the pinned protobuf environment:

```powershell
python tools/auth_fixtures.py --rebuild build/generated/spotify.pb
```

Normal builds only convert checked-in vectors into `generated/auth_vectors.h`
with `--header`; they never regenerate expectations from the implementation or
contact a service. `tools/auth_evidence.py` can reproduce the bounded inspection
containers from the exact pinned DLL; its synthesized files are static analysis
inputs and must never be executed.

Coverage includes granted responses, known hashcash challenges with preserved
state/context, unknown challenges, malformed/oversized messages, scoped-domain
admission, monotonic expiry, deduplicated refresh, client-ID mismatch, refresh
failure, one 401 retry, 403 without refresh, cancellation and logout zeroization.
Live private-service acceptance is a separate, opt-in observation.
