# Bootstrap documentation review — 2026-10-19

Reviewed against commit `ddc25c0` and all 35 GitHub issues before committing the
previously untracked project documents. This review changes documentation and
ignore rules, with no application implementation changes.

## Current guidance

Rewrote the README: its statements that only a smoke test existed, implementation
directories were empty and nanopb/Shannon were absent had been superseded by
issues #1–#29. It now identifies the GUI foundation/headless executables, pinned
build tools, deterministic and opt-in tests, and remaining application/release
issues #30–#35. The roadmap has a dated current-status section and separates
original architecture/memory proposals from implemented contracts, including
the later native Winsock/Schannel CDN transport.

Restored ordinary Markdown in AGENTS.md by removing escaped formatting and
excess blank lines. Its engineering requirements were retained. Kept the useful
local-build/reference/cache and credential exclusions in `.gitignore`, with
root-scoped credential directories so the rules do not hide the vendored
`proto/spotify/login5/v3/credentials` schemas. A tracked-files-versus-ignore
check reports no tracked source files excluded by the resulting rules.

## Retained historical records

- `backlog.json` and `issue-map.json`: all 35 titles, complete bodies after
  newline normalization, labels, milestones and issue URLs match GitHub. All
  dependencies point backward in the issue order. Issues #1–#29 are closed and
  #30–#35 open at review time. The manifests preserve issue creation data and
  intentionally do not claim to be live completion trackers.
- `backlog-verification.json`: preserved as the dated 2026-10-17 baseline
  record. Its backlog SHA-256 still matches the unchanged manifest; a dedicated
  `.gitattributes` rule preserves those bytes across Git newline conversion. Its original
  one-smoke-test result is historical, not the current test suite result.
- `proto-inventory.json`: all 38 files, roots, revision and per-file imports
  match the current pinned `proto/manifest.json` and vendored schemas. The
  implemented protobuf specification references this inventory.
- `xpui-evidence.json`: both retained initial REA record files exist locally
  and match their recorded SHA-256s. Keep the original artifact identities and
  evidence limits; later REA investigations do not invalidate what this earlier
  static pass observed.
- `xpui-static.md` and `transport-and-playback.md`: referenced by existing
  module specifications and GitHub issues. Added historical-scope notices and
  links to later auth, playback, API, Connect and CDN contracts. Initial unknowns,
  source-reference observations and planning policies cannot override those
  implemented contracts.

## Excluded local remnants

The old machine-specific `docs/inspection-readiness.json` contained superseded
MCP launcher/configuration and reconnect advice. The already committed
`docs/rea-mcp-recovery.md` describes the subsequent repair, and later module
specifications retain relevant REA provenance. The old readiness report was
moved to ignored `.cache/documentation-review/inspection-readiness-2026-10-17.json`
for local recovery rather than published as current setup instructions.

Removed seven empty untracked `.gitkeep` files from `docs/specs`, `proto` and
the five source module directories: each directory already contains tracked
files, so placeholders no longer serve a purpose. No source or evidence was
removed with them.

## Checks

JSON parsing, issue/manifest comparison, dependency ordering, protobuf inventory
and imports, retained initial REA file hashes, local Markdown links and staged
whitespace checks pass. A sensitive-value scan found no private-key blocks,
GitHub tokens, JWTs or literal bearer credentials in the retained candidates.
Machine reports, private configuration, build outputs and full local inspection
caches remain ignored. The application code is unchanged from the verified
issue #29 revision. The full Debug/ASan suite passed 47/47 again before this
commit, with no compiler warnings or unexpected ASan errors.
