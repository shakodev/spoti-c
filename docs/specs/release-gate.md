# Release gate evidence and measurement contract (#35)

Recorded before implementation, 2026-10-19. This feature measures this client's
existing implementation; it adds no Spotify endpoint or protocol behavior.

REA MCP `inspect_pe_resources` inspected the existing Release `spotic.exe`:
Evidence `ev_2c6e6bea61d5ef5cd553f0351001fd5071bfbf4263f645ee6802db5ee2a7fa64`, SHA-256
`bdb3165c4477ffa389e4a328d4ef1dddba596c99c14833dfe2ad13ce51d28497`, 985,088 bytes,
PE32+ AMD64. Full receipt: [release-gate-rea.json](release-gate-rea.json).
This establishes artifact identity/resources only, not imports or runtime
performance. REA `inspect_with_objdump(file_headers)` was unavailable on the
configured Windows provider (`ERR_REA_WINDOWS_NATIVE`); it supplied no evidence.
The release audit will separately retain compiler/linker import reports and
runtime loaded-module inventory without attributing them to REA.

Consulted AGENTS.md, CMakeLists.txt, vendor/manifest.json, ROADMAP.md,
transport-and-playback.md, xpui-static.md and the referenced librespot player
source. Existing seek/gapless contracts and fixtures remain authoritative;
this work does not infer new Spotify behavior from that reference.

## Required measurement semantics

Debug ASan correctness precedes Release measurement. Native desktop/GPU and
account/physical-device checks require explicit CMake opt-in. A missing
measurement cannot pass the final release gate. No working-set trimming,
hidden/offscreen measured window, library subtraction or worker relocation.

Read total/peak resident and private resident/commit from Windows process
counters, and CPU user/kernel time from GetProcessTimes. CPU is reported both
as one-core percent and normalized by logical processor count; displayed 0.0%
means normalized CPU strictly below 0.05%, never literal zero decoding cost.
Use QPC elapsed time, reject backwards/zero samples and invalid normalization.
Memory, file-size and startup limits are strict less-than comparisons.

An explicit named benchmark mapping exposes snapshots of the real visible app.
Only boundary snapshot messages wake an idle child; interval sampling runs in
the harness using the process handle. The ending diagnostic wake is excluded
explicitly; all spontaneous wakes, timer timeouts and presents remain counted.
The production event loop continues to wait with INFINITE when idle.

Report renderer-owned logical bytes and swapchain estimates separately from
DXGI local/nonlocal process usage (these are not interchangeable). Retain
machine/OS/driver/audio identity and raw samples, warmup and sample durations.
Warm filesystem-cache launch samples cannot certify an OS-cold start. Missing
cold-start or live-account evidence remains an explicit release failure.

## Measured build identity

The final Release GUI was re-inspected with REA MCP `inspect_pe_resources`:
Evidence `ev_8a5369c5dcb76018b36394ac2d3e09d70c151b5a4f06cd16cc989fd0f44d9f53`, SHA-256
`a4f7c16b8c2f427030a14da627655f9f507891216f910afe9efec90388cbb25c`, 989696 bytes.
Receipt: [release-gate-final-rea.json](release-gate-final-rea.json). As above,
this is artifact identity, not a claim about observed Spotify behavior.

The headless CLI was also inspected with the same REA MCP operation:
Evidence `ev_dd6de7e2746a99f0027d5ee448dcfbc7e0c5234792ac9c5cf03bbf4c8b2fb6d7`, SHA-256
`feafc508454baca76218fcb7e5f49ea86c60833e84a981098022186f11e32227`, 528384 bytes.
Receipt: [release-gate-headless-rea.json](release-gate-headless-rea.json).
The instrumented benchmark harness is a separate binary; its exact hash is
retained in the release audit, and its whole-process counters include the
loopback fixture server and the sampler.
