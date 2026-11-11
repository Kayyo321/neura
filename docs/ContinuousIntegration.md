# Continuous integration

GitHub Actions checks every pull request before it is merged. The workflows are in [`.github/workflows/`](../.github/workflows/), and the scripts they run are in [`scripts/`](../scripts/), so each check can be run from a checkout.

## Tier 1: pull request checks

[`tier1.yml`](../.github/workflows/tier1.yml) runs on every pull request, on every push to `main`, and on merge queue entries. It has three jobs.

| Job | What it runs | Fails when |
| --- | --- | --- |
| Documentation and workflows | `scripts/test-checks.py`, `scripts/check-workflows.py`, `scripts/check-docs.py`, `scripts/update-stdlib-wiki-api.py --check`, and `scripts/generate-wiki.py --check` | A check script misbehaves on its own test inputs; a workflow breaks a [workflow rule](#workflow-rules); a link in a tracked Markdown file names a missing file, wiki page, or heading; a standard library reference page is stale; or a generated wiki page differs from the docs page it is generated from, or `wiki/` holds a page that is neither generated nor listed as hand-written. |
| Build and test (Linux x86-64) | A Release build with GCC 14 on Ubuntu 24.04, then every CTest entry | The build fails; the compiler reports a warning in a file outside `third_party/`; a test fails, including `doc-examples` when a documentation example does not compile or run as its directive says; a test is skipped, disabled, or not run; or the run holds fewer tests than the workflow's `--minimum`. |
| Tier 1 | Reads the results of the other two jobs | Either job ended as anything other than `success`, including `skipped` and `cancelled`. |

**Tier 1** is the status check that a branch rule should require. GitHub treats a skipped job as passing, so requiring the other two jobs directly would let a pull request merge without them having run. The Tier 1 job always runs and fails unless both succeeded.

The CTest entry `doc-examples` runs [`scripts/check-doc-examples.py`](../scripts/check-doc-examples.py), which compiles or runs every fenced `neura` block in `README.md` and `docs/`. Each block must have a directive on the line above it (one blank line is allowed): `<!-- example: check -->` for a complete source file that `neura check` accepts; `<!-- example: run -->` for a program whose exact output under `neura build` and `neura run --deny-all` is the `text` block after it; `<!-- example: test -->` for a test module that `neura test` passes; `<!-- example: rejected: <text> -->` for a file that `neura check` rejects with a diagnostic containing `<text>`; and `<!-- example: fragment: <reason> -->` for a block that is not a complete file, which is not compiled and must say why. The check fails, naming `file:line`, for a block with a missing or unknown directive, a fragment with no reason, or a `run` with no `text` block after it. Each block runs in a fresh temporary directory as `example.nra` with `NEURA_GPU=off` and a 60-second limit.

The build and test job sets `NEURA_LIBCLANG_REQUIRED=1`, so a test that needs libclang fails when the library is missing instead of reporting itself as skipped. It also sets `NEURA_USERNS_REQUIRED=1`: `native-install` runs a static executable in an empty root file system, which needs an unprivileged user namespace, and with the variable set the test fails where the host forbids one instead of leaving those checks out with a warning. Ubuntu 24.04 forbids them through AppArmor, so the job first runs `sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0` on its runner. The job uploads the build log, the CTest JUnit report, and `LastTest.log` as the `tier1-test-report` artifact, including when a step fails.

A pull request that adds CTest entries raises `--minimum` in the workflow to the new count. A pull request that removes entries lowers it, which makes the removal visible in review.

### What Tier 1 does not cover

Tier 1 builds and tests one configuration: Linux on x86-64 with GCC 14. It does not build or test Neura on macOS, on another processor architecture, or with another compiler, and it does not run sanitizers; [Tier 2](#tier-2-platform-and-sanitizer-runs) does, when a maintainer starts it. Release archives are built, tested, and qualified by [Tier 3](#tier-3-release-qualification), which runs for a release tag. No workflow builds or tests Neura on Windows, runs fuzzing, or checks links to other websites.

## Tier 2: platform and sanitizer runs

[`tier2.yml`](../.github/workflows/tier2.yml) has no automatic trigger. A maintainer starts it for a branch or commit with `gh workflow run tier2.yml --ref <branch>`. It has a read-only token and no secrets.

| Job | Configuration |
| --- | --- |
| Build and test (Linux arm64) | Release, GCC 14, Ubuntu 24.04 on arm64, every CTest entry. |
| Build and test (macOS arm64) and (macOS x86-64) | Release, Apple Clang, macOS 15, libffi and pkgconf from Homebrew, every CTest entry CMake registers on macOS. |
| Address and undefined-behavior sanitizers (Linux x86-64) | GCC 14 with `-fsanitize=address,undefined`, without the static launcher, every CTest entry. |

Each job uploads its build log and CTest report. A Tier 2 job does not yet run `check-test-report.py`, so a skipped test does not fail it: read the report before treating a green job as full coverage of that platform. A green Tier 2 run is not required to merge a pull request.

## Run the checks locally

From the repository root, with the [build dependencies](../CONTRIBUTING.md#set-up-a-checkout) installed:

```sh
python3 scripts/test-checks.py
python3 scripts/check-workflows.py
python3 scripts/check-docs.py
python3 scripts/update-stdlib-wiki-api.py --check
python3 scripts/generate-wiki.py --check

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
NEURA_LIBCLANG_REQUIRED=1 NEURA_USERNS_REQUIRED=1 ctest --test-dir build --parallel 4 --no-tests=error --output-on-failure --output-junit ctest-junit.xml
python3 scripts/check-test-report.py build/ctest-junit.xml --minimum 123
```

To check only the documentation examples, run `python3 scripts/check-doc-examples.py --neura build/neura`, or `python3 scripts/check-doc-examples.py --directives-only` to check the directives without a compiler.

The scripts need Python 3 and Git, and use no packages outside the Python standard library. `check-docs.py` reads the Markdown files that Git tracks, so `git add` a new page before checking it. It does not use the network: links to other websites are listed with `--list-external` and are not fetched.

## Tier 3: release qualification

[`release.yml`](../.github/workflows/release.yml) runs for a `v*` tag, and by hand for a branch as a trial that publishes nothing. For each supported target it builds and tests the tagged commit, packs the release archive, and runs the lifecycle checks of [`packaging/qualify.sh`](../packaging/qualify.sh) on the packed archive on a second runner. Its publishing job is the only job in the repository with a token that can write. It runs in the protected `release` environment, after a maintainer approves it. The [release process](ReleaseProcess.md) describes each job and check.

`scripts/check-repository-settings.py` is not part of Tier 1. It compares the repository's GitHub settings with the files in `.github/settings` and needs a signed-in `gh`; see [repository settings](RepositorySettings.md).

## Workflow rules

`scripts/check-workflows.py` requires every workflow to:

- Pin each action and reusable workflow from another repository to a full commit hash, with the release it corresponds to in a comment, and pin each `docker://` image to a digest. A tag or branch name can be moved to different code after review.
- Declare top-level `permissions` with only the scopes the workflow needs, and never `write-all`.
- Declare top-level `concurrency`. Tier 1 uses it to cancel a pull request's run when a newer commit replaces it.
- Give every job `timeout-minutes`.
- Pass `persist-credentials: false` to `actions/checkout`, so the job's token is not left in the checkout for later steps to read.
- Avoid the `pull_request_target` and `workflow_run` triggers. They run with the base repository's credentials, and a workflow that then checks out or builds a pull request's code gives that code those credentials.

A workflow that runs code from a pull request holds no secrets and a read-only token. Tier 1 is such a workflow: the `pull_request` trigger gives a pull request from a fork a read-only token and no secrets, whatever the workflow file requests.
