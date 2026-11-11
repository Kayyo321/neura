# Prompts for AI agents

This directory holds material that teaches AI coding agents to work with Neura.

| Path | Contents |
| --- | --- |
| [`neura/`](neura/) | The Neura language skill: [`SKILL.md`](neura/SKILL.md), the entry point an agent reads first, and [`references/`](neura/references/), which covers syntax, classes (accessors, `last` fields, target-typed `new`, anonymous classes), tags, modules and imports, the standard library, concurrency, the command line, diagnostics and error codes, foreign interop, and style. |
| [`generate-stdlib-api.py`](generate-stdlib-api.py) | Regenerates [`neura/references/stdlib-api.md`](neura/references/stdlib-api.md), the signature index of the standard library. |

## Installing the skill

The [README](../README.md#write-neura-with-an-ai-agent) and the front page of the [website](https://kayyo321.github.io/neura/) carry the prompt that installs the skill: paste it into an agent and the agent downloads this directory and installs it where it loads skills. To install it by hand for Claude Code, copy `prompts/neura` to `~/.claude/skills/neura` (every project) or to `.claude/skills/neura` in a project. Other agents take the same directory in their own skills or rules location; an agent without one can be pointed at `SKILL.md` from its `AGENTS.md`.

## Keeping the skill correct

The skill states what the language specification in [`docs/lang/`](../docs/lang/) and the compiler define, and the specification wins where they differ. When a change alters a user-visible rule, update the skill with the specification.

- Every fenced `neura` block in `prompts/**/*.md` carries an `<!-- example: ... -->` directive, and the `doc-examples` test ([`scripts/check-doc-examples.py`](../scripts/check-doc-examples.py)) compiles, runs, or tests it exactly as it does for `README.md` and `docs/`. Run it with `python3 scripts/check-doc-examples.py --neura build/neura`.
- The diagnostics quoted in [`references/errors-and-diagnostics.md`](neura/references/errors-and-diagnostics.md), with their codes, are the compiler's messages as `neura check --error-format=short` prints them. When a message or code changes, update its row.
- After changing an exported declaration in `stdlib/`, regenerate the API index with `python3 prompts/generate-stdlib-api.py`, and check that it is current with `python3 prompts/generate-stdlib-api.py --check`.
