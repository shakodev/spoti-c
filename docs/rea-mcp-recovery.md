# REA MCP launcher repair (2026-10-17)

Codex's startup log reported `connection closed: initialize response`. The REA
child stderr established the immediate cause: npm `ENOENT` while reading
`%LOCALAPPDATA%\npm-cache\_npx\ad9a3fdbcad992c6\package.json`. The configured
`npx -y rea-agents@6.5.0 mcp` process exited before initializing MCP. This was a
package-runner cache failure, not a missing Ghidra or Java installation.

The repaired per-user installation is pinned to REA 6.5.0 at
`%USERPROFILE%\tools\rea-mcp\6.5.0`. Its public `scripts\rea.mjs` launcher is
invoked directly with the existing Node executable. No npm lookup/download is
required at MCP startup and the launcher no longer depends on a project's cache.

Only REA's entry in `%USERPROFILE%\.codex\config.toml` changed. Its native-provider
environment and all unrelated configuration were preserved and compared after
parsing. The first complete pre-repair backup is
`config.toml.bak-rea-repair-2026-10-17T18-46-48-391Z` alongside the config. Later
scoped backups preserve the intermediate settings as well. No Node/Java/Ghidra
upgrade, Hopper installation, skill replacement, or approval-policy change was
made.

Final registration uses REA's expected 30-second startup policy and a 960-second
tool timeout to cover the existing 900-second native analysis startup allowance.
The direct MCP probe initialized in about eight seconds, advertised all 207 tools,
and successfully called `binary_session`. A scoped `doctor --client codex
--provider ghidra --json` using the installed launcher reported healthy and
verified the existing Ghidra 12.1.4/JDK 25.0.4/native controls.

Use the installed launcher for future diagnostics:

```powershell
& "$env:USERPROFILE/tools/rea-mcp/6.5.0/node_modules/.bin/rea.cmd" doctor --client codex --json
```

Private local diagnostics are retained in ignored `.cache/rea-repair-*` files.
An independent probe verifies the server, not an already-running chat's client.
Restart REA in MCP settings (or restart the app) to replace the failed connection,
then confirm its tools appear in the active session. Official instructions describe
[MCP configuration and restarting the connection](https://learn.chatgpt.com/docs/extend/mcp?surface=cli).
No in-session reconnect capability was available to the repair agent.
