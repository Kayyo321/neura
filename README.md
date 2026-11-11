<h1 align="center">
  <img src="artwork/logo.png" alt="" width="200" height="160">
  <br>
  Neura
</h1>

<p align="center">
  <a href="https://kayyo321.github.io/neura/"><img src="https://img.shields.io/badge/website-kayyo321.github.io%2Fneura-1f29c9" alt="Website: kayyo321.github.io/neura"></a>
  <a href="#build-from-source"><img src="https://img.shields.io/badge/compiler-C%2B%2B20-bf2e70" alt="Compiler: C++20"></a>
  <a href="docs/runtime/NVM.md"><img src="https://img.shields.io/badge/runtime-NVM-087c77" alt="Runtime: Neura Virtual Machine"></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/license-Apache%202.0-965b0c" alt="License: Apache 2.0"></a>
</p>

<p align="center">
  <a href="https://kayyo321.github.io/neura/">Website</a>&nbsp;·
  <a href="#install">Install</a>&nbsp;·
  <a href="docs/lang/Overview.md">Language</a>&nbsp;·
  <a href="docs/lang/StandardLibrary.md">Standard&nbsp;library</a>&nbsp;·
  <a href="examples/">Examples</a>&nbsp;·
  <a href="CONTRIBUTING.md">Contributing</a>
</p>

Neura is a statically typed programming language and a toolchain for compiling its programs into deterministic neuron circuits and decompiling those circuits into readable representations. The compiler derives the topology and weights from the program. The resulting circuits can be run, inspected, tested, and optimized against the behavior defined by the language.

## Install

On Linux, on macOS, and on Windows inside a WSL 2 distribution, one command installs a release:

```sh
curl --proto '=https' --tlsv1.2 -fsSL https://github.com/Kayyo321/neura/releases/latest/download/install.sh | sh
```

It downloads the archive for your machine, verifies it, and installs `neura` under your home directory without `sudo`. That one installation compiles, runs, tests, and decompiles Neura programs. [Install and update](docs/Install.md) lists the supported platforms and describes each step, `neura update`, and `neura uninstall`.

## Your first program

Save this as `hello.nra`:

<!-- example: run -->
```neura
let console = import:Std.Io.Console;
let program = import:Std.Program;

let HelloWorld = module:program.Entry (
    export let.override main = func(args: string[]) -> program.ExitCode {
        console.print_line("Hello, World!");
        return program.ExitCode.Ok;
    };
);
```

```text
Hello, World!
```

Compile it to an image and run the image:

```sh
neura build hello.nra -o hello.nvr
neura run hello.nvr
```

The `.nvr` image contains the circuit and execution information needed by the Neura Virtual Machine. The same installation does the rest:

| Command | What it does |
| --- | --- |
| `neura check hello.nra` | Checks the source without building. |
| `neura test tests.nra` | Runs a test module. |
| `neura build --native hello.nra -o hello` | Packages the image as a native executable that runs without Neura installed (Linux only). |
| `neura decompile hello.nvr` | Recovers readable source from an image. |
| `neura doctor` | Checks the installation and names what optional features need. |
| `neura update` | Installs a newer release. |

[Tooling](docs/lang/Tooling.md) describes every command, and the [tutorial](docs/lang/Tutorial.md) introduces the language.

## Write Neura with an AI agent

The [Neura language skill](https://github.com/Kayyo321/neura/tree/main/prompts/neura) teaches a coding agent the language, the standard library, and the `neura` command line, with examples that the repository's tests compile and run. Paste this prompt into your agent to install it:

```text
Install the Neura language skill, then use it whenever you read or write Neura (.nra) code or run the neura command line.

1. Download the skill: the prompts/neura directory of https://github.com/Kayyo321/neura, which holds SKILL.md and a references directory.
   rm -rf /tmp/neura-skill
   git clone --depth 1 --filter=blob:none --sparse https://github.com/Kayyo321/neura.git /tmp/neura-skill
   git -C /tmp/neura-skill sparse-checkout set prompts/neura

2. Install it where you load skills, keeping SKILL.md and references/ together in a directory named neura:
   - Claude Code: copy /tmp/neura-skill/prompts/neura to ~/.claude/skills/neura for every project, or to .claude/skills/neura in this project.
   - Another agent with a skills or rules directory: copy it there.
   - An agent without skills: copy it to .agents/skills/neura in this project, and add this line to AGENTS.md, creating the file if needed: "Before reading or writing Neura (.nra) code, read .agents/skills/neura/SKILL.md and follow it."
   Then remove /tmp/neura-skill.

3. Run `neura --version`. If neura is not installed, offer to install it with the official installer, which installs under the home directory without sudo:
   curl --proto '=https' --tlsv1.2 -fsSL https://github.com/Kayyo321/neura/releases/latest/download/install.sh | sh

4. Read SKILL.md now. Then tell me where you installed the skill and which neura version you found.
```

An agent reads the skill's `SKILL.md` first and its `references/` as a task needs them. The [`prompts`](https://github.com/Kayyo321/neura/tree/main/prompts) directory describes how the skill is kept up to date.

## Compilation and execution

```text
Lexer -> Parser -> NeuraIR -> IR optimization -> IRtoNeuronConversion -> output
```

The compiler checks source semantics before producing typed NeuraIR. Optimization preserves the program's observable behavior, and the conversion stage maps the optimized IR to neuron primitives. The output describes the circuit, its weights, and the inputs, state, and runtime capabilities it needs.

Pure computations can use feedforward circuits. Stateful execution carries mutable data and control state across steps. Classes share method circuits while each instance holds its own fields. Decompilation recovers behavior from supported artifacts and can use optional metadata to improve readability. Verification compares the recovered representation with the circuit over a defined input and state domain.

## Build from source

Installing a release is the way to use Neura. Build from source to work on Neura itself, or on a platform that has no release archive. [CONTRIBUTING.md](CONTRIBUTING.md#set-up-a-checkout) covers the checkout, its submodule, and the build's dependencies in full. In short, with CMake 3.20 or newer, C and C++20 compilers, and the libffi development package:

```sh
git clone --recursive https://github.com/Kayyo321/neura.git
cd neura
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
build/neura --version
```

A source build reports its version as `<version>-dev`, and `neura update` does not manage it.

## Documentation

- [Project design](docs/Project.md) covers the pipeline, execution model, decompilation, and verification.
- [Language overview](docs/lang/Overview.md) and [tutorial](docs/lang/Tutorial.md) cover Neura syntax and semantics. See [examples](examples/) for sample `.nra` programs.
- [Tooling](docs/lang/Tooling.md) covers building the compiler, checking source, inspecting NeuraIR, building and running `.nvr` images, running tests, and disassembling and decompiling images.
- [Neura Virtual Machine](docs/runtime/NVM.md) and [NVR format](docs/runtime/NVR.md) define execution and the executable image. [Neuron conversion and execution](docs/Backend.md) and [Decompiler](docs/Decompiler.md) describe the implementation, and the [release checklist](docs/Release.md) maps each feature to its tests.
- [Install, update, and uninstall](docs/Install.md) covers installing a release archive, `neura doctor`, `neura update`, and `neura uninstall`.
- [Wiki maintenance](docs/Wiki.md) explains how to update, review, and publish the repository's wiki sources.
- [Website maintenance](docs/Site.md) explains how to change, preview, and publish the [website](https://kayyo321.github.io/neura/), whose pages are in [`site/`](site/) and whose documentation is this repository's Markdown, rendered. [The design of the website](docs/SiteDesign.md) records its colors, type, grids, and illustrations.
- [Contributing](CONTRIBUTING.md) explains how to propose changes and verify them.

## Rules for agents

- Read [AGENTS.md](AGENTS.md), the relevant language spec, and [Coding style](docs/style/CodingStyle.md) before changing compiler behavior or `.nra` examples.
- Preserve the pipeline boundaries. Keep source semantics in typed NeuraIR, preserve observable behavior during optimization, and lower program logic into executable neuron primitives.
- Check semantic changes with a representative program and a failure or boundary case. Report which checks actually ran and their scope. Identify unresolved language behavior before relying on it.
- Keep language rules, examples, and affected stage contracts in sync when a change alters behavior.

## License

[Apache License 2.0](LICENSE)
