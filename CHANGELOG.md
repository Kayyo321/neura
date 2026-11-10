# Changelog

This file records the changes in each version of Neura that a user of the language, the `neura` command, the standard library, or the `.nir` and `.nvr` formats can observe. Its layout follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Versions are numbered `MAJOR.MINOR.PATCH`. Before `1.0.0`, a minor version may change or remove existing behavior, and its entry lists each such change under **Changed** or **Removed**. The `.nir` and `.nvr` formats carry their own version numbers, which the [NeuraIR format](docs/NirFormat.md) and [NVR format](docs/runtime/NVR.md) pages define.

A pull request that changes observable behavior adds a line to **Unreleased** under one of these headings:

| Heading | For |
| --- | --- |
| Added | New syntax, library members, commands, options, or format features. |
| Changed | Existing behavior that now differs, including a diagnostic or exit status that programs or scripts may depend on. |
| Deprecated | Behavior that still works and is scheduled for removal. |
| Removed | Behavior that no longer exists. |
| Fixed | Behavior corrected to match its specification. |
| Security | Fixes for vulnerabilities reported under the [security policy](SECURITY.md). |

## Unreleased

No version has been published. The [release checklist](docs/Release.md) lists the language, toolchain, standard library, and decompiler features on `main`, each with the tests that establish it.
