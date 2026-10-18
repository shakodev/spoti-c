# Native CDN transport

User-authorized architecture change for issue #19, 2026-10-18. Spotify JSON,
authentication and internal API traffic remains on WinHTTP. CDN audio ranges
use a bounded Winsock/Schannel transport in both live and fixture modes. This
introduces no Spotify endpoint, header or response shape; existing contracts
are in `audio-range-cache.md` and `headless-playback.md`.

One owned worker admits one GET range at a time. Request/response headers,
TLS records, DNS candidates and body storage have fixed caps. Requests carry
only Host, Range, Accept-Encoding: identity, Connection: close and User-Agent;
account credentials and custom headers are rejected. HTTP is accepted only for
explicit IPv4 loopback fixtures. Production requires HTTPS. Redirects, chunked
framing, non-identity encoding, ambiguous framing and oversized inputs fail
explicitly. Existing range validation still checks 206/416, Content-Range,
representation identity, exact byte counts and cancellation generations.

The worker uses nonblocking sockets and cancel/stop/network events with an
absolute request deadline. DNS uses cancellable GetAddrInfoExW and bounded
address candidates. Cancellation is acknowledged only after the worker stops
using the caller's body buffer. Shutdown joins before releasing storage.
If an OS provider fails to retire within the shutdown timeout, live storage is
retained and failure is explicit, rather than freeing a worker's buffers.

TLS uses Windows Schannel, TLS 1.2, automatic server certificate/hostname
validation, strong crypto, no default client credentials, and chain revocation
checking. No manual-validation or certificate-error bypass is present.
Client-certificate requests, renegotiation and records/handshakes above fixed
caps fail closed. TLS and WinHTTP imports are demand-loaded in headless builds,
so plaintext offline fixtures do not initialize either stack. Complete process
residency is measured, including all DLLs actually used; nothing is subtracted.

Primary platform contracts:

- https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfoexw
- https://learn.microsoft.com/en-us/windows/win32/secauthn/initializesecuritycontext--schannel
- https://learn.microsoft.com/en-us/windows/win32/secauthn/performing-authentication-using-schannel
- https://learn.microsoft.com/en-us/windows/win32/secauthn/encryptmessage--schannel
- https://learn.microsoft.com/en-us/windows/win32/secauthn/decryptmessage--schannel

Correctness uses deterministic local HTTP, parser boundary/fault cases, repeated
cancel/shutdown, strict warnings and ASan. An independent local Python/OpenSSL
peer presents a public synthetic certificate; the production Schannel path must
return `SEC_E_UNTRUSTED_ROOT` and release security handles. No trust store changes
are made. A separate explicit `ENABLE_LIVE_CDN_TEST` checks a public trusted
HTTPS range. Account playback remains opt-in; absent credentials are recorded
as skipped. These tests do not establish account or audible-device compatibility.
