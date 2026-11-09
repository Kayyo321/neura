# Tooling and project workflow

These are proposed interfaces, not commands currently available in this repository. Names, flags and manifest syntax are placeholders for compiler planning; no package manager or build tool exists yet.

A minimal workflow should support creating or arranging a source root, checking and running an entry-point module, running an explicit list of test modules, and building an executable. For example, a future tool might expose `neura check`, `neura run`, `neura test`, and `neura build`; their arguments and behavior are open. A successful build should produce a reproducible artifact for a specified target and a nonzero exit on errors. Dependency resolution, lockfiles, manifest format, target selection and packaging require separate designs; see [Project structure](ProjectStructure.md).

Diagnostics should identify the source file, line and column, the offending span, a clear explanation and, where possible, a correction. For example, passing a `string` to a function expecting `int32` should report both types and the argument location. Diagnostics go to standard error and compilation fails on errors. Exact format, encoding of column offsets and warning policy remain open.

A formatter should produce stable, idempotent output consistent with [Coding style](../style/CodingStyle.md). Editor integration should eventually expose diagnostics, navigation and completion; debugger support should include source locations, stepping and local variables. These are tooling goals, not promises of first-release support.
