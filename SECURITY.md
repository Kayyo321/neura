# Security policy

## Supported versions

Security fixes are made on the `main` branch and issued in a new release. Only the most recent [release](https://github.com/Kayyo321/neura/releases) receives fixes.

## Report a vulnerability

Neura handles security reports in the open. Report a suspected vulnerability by opening a [security report](https://github.com/Kayyo321/neura/issues/new?template=security-report.yml) in the issue tracker. The report, the discussion, and the fix are public from the start. The project has no private reporting channel.

Include what a maintainer needs to reproduce the problem:

- The commit of Neura you built (`git rev-parse HEAD`), the operating system, the processor architecture, and the C++ compiler.
- The smallest input that triggers the problem: `.nra` source, a `.nir` file, an `.nvr` image, assembly text, a C header, or a recorded trace.
- The exact command line, including every `--allow-*` and `--max-*` option.
- What happened, what you expected, and what an attacker gains.

Leave out anything that is not yours to publish: credentials, personal data, and the contents of other people's systems. Reduce the input to one that shows the problem without them.

A maintainer replies in the issue and labels it once the problem is confirmed or declined against the scope below. The fix is a pull request linked to the issue, and its [changelog](CHANGELOG.md) entry goes under **Security** and credits you unless you ask otherwise.

## What counts as a vulnerability

Neura reads files that may come from someone else and runs programs under a set of granted capabilities. A report is in scope when one of those boundaries fails:

| Boundary | In scope |
| --- | --- |
| Reading input | Memory corruption, an out-of-bounds read or write, or undefined behavior in `neura` caused by a crafted `.nra` source file, `.nir` file, `.nvr` image, assembly text, or trace file. The [NVR loader](docs/runtime/NVR.md#writer-and-loader-requirements) is specified to reject malformed images. |
| Capabilities | A running image that reaches a file, socket, environment variable, clock, entropy source, or native call without the matching [`--allow-*` option](docs/lang/Tooling.md#build-and-run-executables), or that reaches a path outside the prefixes given to `--allow-fs=prefix,...`. |
| Limits | A running image that exceeds a configured frame, heap, or step limit without trapping, or that makes the runner use host memory without bound. See [determinism and limits](docs/runtime/NVM.md#determinism-limits-and-observability). |
| Replay | A run under `neura run --replay` that reaches a file, socket, environment variable, clock, entropy source, or native call. A replay answers those requests from the trace and needs no capability. |

These are outside the policy, and are ordinary bug reports when they are defects at all:

- Anything native code does after `--allow-native` is granted. [Native code runs outside Neura's guarantees](docs/lang/ForeignInterop.md#safety-and-determinism): it can read and write the runner's memory and end the process.
- What a program does with a capability it was granted, such as deleting files under `--allow-fs`.
- A program that runs until it reaches a configured limit, or a compilation that is slow for a large input.
- A crash, a wrong result, or a rejected valid program with no memory-safety or capability consequence. Report these with the [bug report](https://github.com/Kayyo321/neura/issues/new?template=bug-report.yml) form.
- Vulnerabilities in the system libclang, libffi, or the C++ toolchain. Report those to their projects; report here if Neura's use of them is at fault.
