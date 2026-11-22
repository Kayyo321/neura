# Continuous integration

GitHub Actions checks every pull request before it is merged and publishes the [website](Site.md) from `main`. The workflows are in [`.github/workflows/`](../.github/workflows/), and the scripts they run are in [`scripts/`](../scripts/), so each check can be run from a checkout.

## Tier 1: pull request checks

[`tier1.yml`](../.github/workflows/tier1.yml) runs on every pull request, on every push to `main`, and on merge queue entries.

| Job | What it runs | Fails when |
| --- | --- | --- |
| Documentation and workflows | `scripts/test-checks.py`, `scripts/check-workflows.py`, `scripts/check-docs.py`, `scripts/update-stdlib-wiki-api.py --check`, `scripts/generate-wiki.py --check`, `scripts/gen-kernels.py --check`, and `scripts/build-site.py --check` | A check script misbehaves on its own test inputs; a workflow breaks a [workflow rule](#workflow-rules); a link in a tracked Markdown file names a missing file, wiki page, or heading; a standard library reference page is stale; a generated wiki page differs from the docs page it is generated from, or `wiki/` holds a page that is neither generated nor listed as hand-written; or the kernel table, the kernel reference, or the `Std.Internal.Kernels` declarations differ from what `src/kernels/` generates. |
| Build and test (Linux x86-64) | A Release build with GCC 14 on Ubuntu 24.04 with the server components of the release archives (TLS, HTTP/2 and the PostgreSQL provider, built by `packaging/build-server-components.sh` and cached under a key of the component lock and that script; [server host](runtime/ServerHost.md#release-archives)), `scripts/check-kernels.sh`, `scripts/check-core-closure.py`, then every CTest entry | The build fails; the compiler reports a warning in a file outside `third_party/`; the kernel archive calls a libm function or a libc function other than the memory builtins; a binary of the execution core holds an object of a compiler library or loads `libneura-sdk`, or `libneura-sdk` holds a copy of a core object ([tools](lang/Tooling.md#tools-and-the-neura-dispatcher)); a test fails, including `doc-examples` when a documentation example does not compile or run as its directive says; a test is skipped, disabled, or not run; or the run holds fewer tests than the workflow's `--minimum`. |
| Runtime-only build (Linux x86-64) | A Release build with `-DNEURA_SDK=OFF` and no submodule, `scripts/check-core-closure.py`, an install of the `core` component, `neura --version`, `neura doctor`, and `neura build`, which must fail and name the missing SDK | The core does not build or install without the SDK; the build reports a warning; `doctor` fails; or `neura build` does not report the missing SDK. |
| Build and test (Windows x86-64) | `windows-msvc` on `windows-2022`, the repository checks, kernel and core closure checks, the Windows binary layout check, and CTest with `NEURA_TEST_TIMEOUT_SCALE=12`, except the twelve tests that take most of the suite's time without the JIT (the `stdlib-crypto-*` suites, `stdlib-character`, `backend-reference` and `lifetime-programs`), which Tier 2 runs | Neura's sources emit an MSVC warning, a binary violates the Windows layout, or a test is skipped, fails, or is missing. |
| Runtime-only build (Windows x86-64) | `windows-msvc` with `NEURA_SDK=OFF`, `core` installation and execution of an `.nvr` built by the SDK job | The core needs the SDK to build or run, a dependency or warning check fails, or `neura build` does not report the missing SDK. |
| Tier 1 | Reads the results of the other five jobs | Any job ended as anything other than `success`, including `skipped` and `cancelled`. |

**Tier 1** is the status check that a branch rule should require. The aggregator always runs and fails unless the documentation and workflow job, both Linux jobs and both Windows jobs succeeded. The Windows jobs joined it after their first green run (run 44973177211).

The CTest entry `doc-examples` runs [`scripts/check-doc-examples.py`](../scripts/check-doc-examples.py), which compiles or runs every fenced `neura` block in `README.md`, `docs/`, and `prompts/`. Each block must have a directive on the line above it (one blank line is allowed): `<!-- example: check -->` for a complete source file that `neura check` accepts; `<!-- example: run -->` for a program whose exact output under `neura build` and `neura run --deny-all` is the `text` block after it; `<!-- example: test -->` for a test module that `neura test` passes; `<!-- example: rejected: <text> -->` for a file that `neura check` rejects with a diagnostic containing `<text>`; and `<!-- example: fragment: <reason> -->` for a block that is not a complete file, which is not compiled and must say why. The check fails, naming `file:line`, for a block with a missing or unknown directive, a fragment with no reason, or a `run` with no `text` block after it. Each block runs in a fresh temporary directory as `example.nra` with `NEURA_GPU=off` and a 60-second limit.

The build and test job sets `NEURA_LIBCLANG_REQUIRED=1`, so a test that needs libclang fails when the library is missing instead of reporting itself as skipped. It also sets `NEURA_USERNS_REQUIRED=1`: `native-install` runs a static executable in an empty root file system, which needs an unprivileged user namespace, and with the variable set the test fails where the host forbids one instead of leaving those checks out with a warning. Ubuntu 24.04 forbids them through AppArmor, so the job first runs `sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0` on its runner. The job uploads the build log, the CTest JUnit report, and `LastTest.log` as the `tier1-test-report` artifact, including when a step fails.

A pull request that adds CTest entries raises `--minimum` in the workflow to the new count. A pull request that removes entries lowers it, which makes the removal visible in review.

### What Tier 1 does not cover

Tier 1 builds Linux x86-64 with GCC 14 and Windows x86-64 with MSVC. [Tier 2](#tier-2-platform-and-sanitizer-runs) covers macOS, Linux arm64, clang-cl on Windows, and sanitizers when a maintainer starts it. [Tier 3](#tier-3-release-qualification) builds and qualifies release archives.

## Tier 2: platform and sanitizer runs

[`tier2.yml`](../.github/workflows/tier2.yml) has no automatic trigger. A maintainer starts it for a branch or commit with `gh workflow run tier2.yml --ref <branch>`. It has a read-only token and no secrets.

| Job | Configuration |
| --- | --- |
| Build and test (Linux arm64) | Release, GCC 14, Ubuntu 24.04 on arm64, `NEURA_TEST_TIMEOUT_SCALE=12`, a 360-minute limit, every CTest entry. |
| Build and test (macOS arm64) and (macOS x86-64) | Release, Apple Clang, macOS 15, libffi and pkgconf from Homebrew, `NEURA_TEST_TIMEOUT_SCALE=12`, a 360-minute limit, every CTest entry CMake registers on macOS; the x86-64 job leaves out the crypto suites that release.yml also leaves out there ([tests that run on fewer targets](#tests-that-run-on-fewer-targets)). |
| Build and test (Windows `windows-msvc` and `windows-clang-cl`) | Release, `windows-2022`, `NEURA_TEST_TIMEOUT_SCALE=12`, a 360-minute limit, CTest and the Windows binary layout and core closure checks; like the x86-64 macOS job, it leaves out the crypto suites that release.yml also leaves out there ([tests that run on fewer targets](#tests-that-run-on-fewer-targets)). |
| Address and undefined-behavior sanitizers (Linux x86-64, shards 1 to 6) | GCC 14 with `-fsanitize=address,undefined`, without the static launcher or the static dispatcher (`NEURA_STATIC_DISPATCHER=OFF`), with a 1 GiB stack and every time limit multiplied by 6 (`NEURA_TEST_TIMEOUT_SCALE=6`): 5.5 hours for `backend-reference`, 3 hours for `decompiler-behavior` and `lifetime-programs`, 2 hours for `program-fuzz`, every CTest entry except `native-install` and `native-container`. Each shard builds the same configuration and runs every sixth test by CTest number. |

The sanitizer job leaves out two tests that cannot hold in its configuration: `native-install` requires a launcher that needs only the C library, and `native-container` rewrites an executable it has just run, which the leak checker's helper process can still hold open. It raises the stack because sanitized frames are several times larger, and `backend-reference` runs test programs 400 calls deep in an evaluator that recurses on the native stack. It sets `NEURA_TEST_SANITIZERS=1`, which skips two checks a sanitized build cannot meet: `server-host`'s keep-alive latency limit of 30 ms, and `backend-host`'s check that secret memory is locked, because AddressSanitizer makes `mlock` do nothing.

Each job uploads its build log and CTest report. The macOS and Windows jobs run `check-test-report.py`. In the Linux arm64 and sanitizer jobs a skipped test does not fail the job, so read the report before treating a green job as full coverage of that configuration. A green Tier 2 run is not required to merge a pull request.

## Native code: the JIT on other platforms

[`jit.yml`](../.github/workflows/jit.yml) runs tier 2's native code generator ([native compilation](runtime/NVM.md#native-compilation)) where `--jit=auto` does not enable it yet: Ubuntu 24.04 on arm64 (GCC 14) and macOS 15 on arm64 and x86-64 (Apple Clang). It runs for a pull request that changes `src/jit/` or `src/nvm/`, and when a maintainer starts it with `gh workflow run jit.yml --ref <branch>`. It has a read-only token and no secrets, and it is not required to merge a pull request.

Each job builds `neura` and the backend tests, runs the `jit` and `tier` suites with `NEURA_JIT=on`, runs `scripts/tier-boundary.py --jit off,on`, and captures the extended runtime identity cases at `--tier=0` and with `--jit on` (once more with `NEURA_TIER_EAGER=1`) and compares them byte for byte. When a platform's job passes, `auto` can be extended to it.

## Pages: website deployment

[`pages.yml`](../.github/workflows/pages.yml) runs on every push to `main`, and when a maintainer starts it by hand. It builds the [website](Site.md) from `site/` and the documents it lists, and deploys it with GitHub Pages. It does not run for pull requests; Tier 1 checks the site there.

| Job | What it runs | Token permissions |
| --- | --- | --- |
| Build the site | `actions/configure-pages` to read the path the site is served below, `scripts/build-site.py --output _site`, and `actions/upload-pages-artifact` | `contents: read` and `pages: read` |
| Deploy to GitHub Pages | `actions/deploy-pages` on the uploaded artifact, in the `github-pages` environment | `pages: write` and `id-token: write` |

The deploy job is the only job in the repository whose token can write. It checks out nothing and runs no script from the repository: it deploys the artifact that the build job made from a commit on `main`. The `github-pages` environment accepts a deployment from `main` only. That rule is a [repository setting](Site.md#repository-settings-the-site-depends-on), so a workflow file on another branch cannot lift it.

The runs share one concurrency group and a run is not cancelled once it has started, so a deployment finishes before the next one begins.

## Run the checks locally

From the repository root, with the [build dependencies](../CONTRIBUTING.md#set-up-a-checkout) installed:

```sh
python3 scripts/test-checks.py
python3 scripts/check-workflows.py
python3 scripts/check-docs.py
python3 scripts/update-stdlib-wiki-api.py --check
python3 scripts/generate-wiki.py --check
python3 scripts/gen-kernels.py --check
python3 scripts/build-site.py --check

sh packaging/build-server-components.sh ~/server-components
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DNEURA_ENABLE_SERVER_TLS=ON -DOPENSSL_ROOT_DIR=$HOME/server-components/openssl \
  -DNEURA_ENABLE_SERVER_HTTP2=ON -DNEURA_NGHTTP2_ROOT=$HOME/server-components/nghttp2 \
  -DNEURA_ENABLE_POSTGRES=ON -DNEURA_LIBPQ_SOURCE=$HOME/server-components/postgresql \
  -DNEURA_LIBPQ_BUILD=$HOME/server-components/postgresql \
  -DNEURA_COMPONENT_LICENSES=$HOME/server-components/licenses \
  -DNEURA_SERVER_TEST_OPENSSL=$HOME/server-components/openssl/bin/openssl
cmake --build build --parallel
scripts/check-kernels.sh build
python3 scripts/check-core-closure.py build
NEURA_LIBCLANG_REQUIRED=1 NEURA_USERNS_REQUIRED=1 ctest --test-dir build --parallel 4 --no-tests=error --output-on-failure --output-junit ctest-junit.xml
python3 scripts/check-test-report.py build/ctest-junit.xml --minimum 326
```

The first two commands build the configuration of the Tier 1 job; `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` alone builds without TLS, HTTP/2 and the PostgreSQL provider, which leaves out their tests (`server-sql-postgres` also needs Docker). To check only the documentation examples, run `python3 scripts/check-doc-examples.py --neura build/neura`, or `python3 scripts/check-doc-examples.py --directives-only` to check the directives without a compiler.

The scripts need Python 3 and Git, and use no packages outside the Python standard library. `check-docs.py` reads the Markdown files that Git tracks, so `git add` a new page before checking it. It does not use the network: links to other websites are listed with `--list-external` and are not fetched.

From a Visual Studio 2022 developer PowerShell with the [Windows build prerequisites](design/Windows030.md#toolchain-and-build) installed, run:

```powershell
cmake --preset windows-msvc -DNEURA_TEST_TIMEOUT_SCALE=12
cmake --build --preset windows-msvc
python scripts/check-windows-binaries.py build-windows-msvc
python scripts/check-core-closure.py build-windows-msvc
ctest --preset windows-msvc --parallel 6 --output-on-failure
```

The Windows CI jobs add build and test logs and use `NEURA_LIBCLANG_REQUIRED=1` with a supported Visual Studio libclang. A local Smart App Control block on an unsigned executable or DLL leaves the test unverified on that machine. Record the block and use the `windows-2022` CI result for that check.

## Tier 3: release qualification

[`release.yml`](../.github/workflows/release.yml) runs for a `v*` tag, and by hand for a branch as a trial that publishes nothing. On Linux and macOS it builds the pinned server components, packs three archives per target, and runs [`packaging/qualify.sh`](../packaging/qualify.sh) on a second runner. On Windows it builds the SDK and runtime archives with `windows-release` and runs [`packaging/qualify.py`](../packaging/qualify.py) on a second `windows-2022` runner. The publishing job runs in the protected `release` environment after a maintainer approves it. The [release process](ReleaseProcess.md) describes the individual checks and Windows exceptions.

### Time limits

Every test's CTest time limit is set for a developer machine with the x86-64 JIT. The CMake cache variable `NEURA_TEST_TIMEOUT_SCALE` (a whole number, 1 by default) multiplies each limit, including CTest's default of 1,500 seconds for a test without one, and is passed to each test in its environment. `release.yml` sets it per target in its `timeout-scale` matrix value: 3 on x86-64 Linux, 12 on arm64 Linux and both macOS targets, where pure-Neura code runs without the JIT on runners with 3 or 4 cores. Tier 2's arm64 Linux and macOS jobs use the same scale of 12 and the same 360-minute limit; its sanitizer jobs use a scale of 6 and the same limit. The build job's limit is 360 minutes, the most a hosted runner allows. Tier 1's build and test job runs the same suite on the same x86-64 Linux runner with a scale of 3 and a limit of 120 minutes; its tests take about 55 minutes there. The longest tests carry a CTest `COST`, so `ctest --parallel` starts them first, and the three SLH-DSA signing files of `stdlib-crypto-pqc` run as entries of their own (`stdlib-crypto-pqc-slh-dsa-sign-fast`, `-sha2-small` and `-shake-small`), so that area is not one entry of hours without the JIT.

### Tests that run on fewer targets

| Test or case | Where it runs | Why, and where the same code is covered |
| --- | --- | --- |
| `server-sql-postgres` | Linux | The PostgreSQL servers it starts run in Docker, which the macOS runners lack. Qualification check 15 checks that the provider is present in the Linux and macOS archives. Windows 0.3.0 defers the PostgreSQL provider. |
| `stdlib-crypto-advanced`, `stdlib-crypto-asymmetric`, `stdlib-crypto-pqc` and its `stdlib-crypto-pqc-slh-dsa-sign-*` parts | Every target but x86-64 macOS and Windows | Windows has no JIT in 0.3.0, and on the interpreter `stdlib-crypto-asymmetric` alone ran for 127 minutes on arm64 macOS, more than a quarter of the suite's 494 test-minutes; the Windows interpreter is the same code, built by MSVC. Without the JIT each takes hours on the Intel runner, which ran the crypto suites five to six times slower than the arm one in release trial 21. There `stdlib-crypto-advanced` was still running when the job reached its 360-minute limit, after every other test had passed; it took 3,034 s on arm macOS and 2,618 s on arm Linux. They are pure Neura code with no host services, so the arm64 macOS run (same compiler, C++ library and interpreter, without the JIT) and the x86-64 Linux run (same processor architecture) cover what they test; that is an inference, not a run on x86-64 macOS. |
| `backend-host` | Linux and macOS | Its cases drive the host services with POSIX sockets, processes and files, and the file holds no case on Windows, so CMake does not register it there. The service code it checks is shared with Windows; on Windows the platform layer under it runs in `platform-file`, `platform-socket`, `platform-process` and `platform-environment`, and the services themselves run end to end in `stdlib-file`, `stdlib-fs`, `stdlib-host`, `stdlib-net-socket`, `stdlib-environment` and `stdlib-clock`. That is coverage through other entries, not a run of these cases on Windows. |
| `server-host`: `test_refused_assignment` | Linux | Its test hook refuses the Assign with `shutdown(SHUT_RD)`, which makes the host's send fail on Linux only; on macOS the send succeeds and the request ends at its deadline. The retry it checks is the host's own code, the same on every system, and runs on both Linux targets. |
| `server-host`: `test_dead_ready_worker`, `test_zygote_restart`, `test_process_bound`, the zygote cases | Linux | They read `/proc` or use the worker zygote, which is Linux-only ([server host](runtime/ServerHost.md)). |
| POSIX `service-*` suites, native launcher execution | Linux | The POSIX service suites read `/proc` and use `memfd_create`. The Windows service has separate tests. macOS and Windows have no native launcher in 0.3.0. |
| TLS, HTTP/2 and desktop host cases | Linux and macOS as applicable | Windows 0.3.0 defers TLS, HTTP/2 and desktop hosting. Tests for the Windows refusals cover the applicable command and service entry points. |

`scripts/check-repository-settings.py` is not part of Tier 1. It compares the repository's GitHub settings with the files in `.github/settings` and needs a signed-in `gh`; see [repository settings](RepositorySettings.md).

## Workflow rules

`scripts/check-workflows.py` requires every workflow to:

- Pin each action and reusable workflow from another repository to a full commit hash, with the release it corresponds to in a comment, and pin each `docker://` image to a digest. A tag or branch name can be moved to different code after review.
- Declare top-level `permissions` with only the scopes the workflow needs, and never `write-all`.
- Declare top-level `concurrency`. Tier 1 uses it to cancel a pull request's run when a newer commit replaces it.
- Give every job `timeout-minutes`.
- Pass `persist-credentials: false` to `actions/checkout`, so the job's token is not left in the checkout for later steps to read.
- Avoid the `pull_request_target` and `workflow_run` triggers. They run with the base repository's credentials, and a workflow that then checks out or builds a pull request's code gives that code those credentials.

A workflow that runs code from a pull request holds no secrets and a read-only token. Tier 1 is such a workflow: the `pull_request` trigger gives a pull request from a fork a read-only token and no secrets, whatever the workflow file requests. [Pages](#pages-website-deployment) holds the repository's only write permissions and never runs for a pull request.
