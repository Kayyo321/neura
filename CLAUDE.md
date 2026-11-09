# Claude Code instructions

@AGENTS.md

Use the imported repository instructions for every task. Start with the source that owns the contract you are changing.

- For source-language work, read the relevant [language specification page](docs/lang/Overview.md) and its examples before editing syntax, name resolution, typing, or diagnostics.
- For compiler, runtime, or artifact work, read the relevant sections of [the project specification](docs/Project.md). Identify the affected stage and the contract it exposes to the next stage before making the change.
- For decompiler or verification work, trace which evidence comes from the executable circuit and which comes from optional metadata. Scope your claims to the comparison or proof you can perform.

In a handoff or completion summary, name the affected compiler stages or language rules and report the checks that ran. When a check requires tooling that is unavailable, identify the specific verification still needed. Keep shared policy in [AGENTS.md](AGENTS.md).
