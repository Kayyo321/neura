# Continuous integration

GitHub Actions checks every pull request before it is merged. The workflows are in [`.github/workflows/`](../.github/workflows/), and the scripts they run are in [`scripts/`](../scripts/), so each check can be run from a checkout.

## Tier 1: pull request checks

[`tier1.yml`](../.github/workflows/tier1.yml) runs on every pull request, on every push to `main`, and on merge queue entries. It has three jobs.

| Job | What it runs | Fails when |
| --- | --- | --- |
| Documentation and workflows | `scripts/test-checks.py`, `scripts/check-workflows.py`, `scripts/check-docs.py`, and `scripts/update-stdlib-wiki-api.py --check` | A check script misbehaves on its own test inputs; a workflow breaks a [workflow rule](#workflow-rules); a link in a tracked Markdown file names a missing file, wiki page, or heading; or a standard library reference page is stale. |
| Build and test (Linux x86-64) | A Release build with GCC 14 on Ubuntu 24.04, then every CTest entry | The build fails; the compiler reports a warning in a file outside `third_party/`; a test fails; a test is skipped, disabled, or not run; or the run holds fewer tests than the workflow's `--minimum`. |
| Tier 1 | Reads the results of the other two jobs | Either job ended as anything other than `success`, including `skipped` and `cancelled`. |

**Tier 1** is the status check that a branch rule should require. GitHub treats a skipped job as passing, so requiring the other two jobs directly would let a pull request merge without them having run. The Tier 1 job always runs and fails unless both succeeded.

The build and test job sets `NEURA_LIBCLANG_REQUIRED=1`, so a test that needs libclang fails when the library is missing instead of reporting itself as skipped. It also sets `NEURA_USERNS_REQUIRED=1`: `native-install` runs a static executable in an empty root file system, which needs an unprivileged user namespace, and with the variable set the test fails where the host forbids one instead of leaving those checks out with a warning. Ubuntu 24.04 forbids them through AppArmor, so the job first runs `sudo sysctl -w kernel.apparmor_restrict_unprivileged_userns=0` on its runner. The job uploads the build log, the CTest JUnit report, and `LastTest.log` as the `tier1-test-report` artifact, including when a step fails.

A pull request that adds CTest entries raises `--minimum` in the workflow to the new count. A pull request that removes entries lowers it, which makes the removal visible in review.

### What Tier 1 does not cover

Tier 1 builds and tests one configuration: Linux on x86-64 with GCC 14. No workflow builds or tests Neura on macOS, on Windows, on another processor architecture, or with another compiler. No workflow runs sanitizers or fuzzing, checks links to other websites, or builds release artifacts.

## Run the checks locally

From the repository root, with the [build dependencies](../CONTRIBUTING.md#set-up-a-checkout) installed:

```sh
python3 scripts/test-checks.py
python3 scripts/check-workflows.py
python3 scripts/check-docs.py
python3 scripts/update-stdlib-wiki-api.py --check

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
NEURA_LIBCLANG_REQUIRED=1 NEURA_USERNS_REQUIRED=1 ctest --test-dir build --parallel 4 --no-tests=error --output-on-failure --output-junit ctest-junit.xml
python3 scripts/check-test-report.py build/ctest-junit.xml --minimum 114
```

The scripts need Python 3 and Git, and use no packages outside the Python standard library. `check-docs.py` reads the Markdown files that Git tracks, so `git add` a new page before checking it. It does not use the network: links to other websites are listed with `--list-external` and are not fetched.

## Workflow rules

`scripts/check-workflows.py` requires every workflow to:

- Pin each action and reusable workflow from another repository to a full commit hash, with the release it corresponds to in a comment, and pin each `docker://` image to a digest. A tag or branch name can be moved to different code after review.
- Declare top-level `permissions` with only the scopes the workflow needs, and never `write-all`.
- Declare top-level `concurrency`. Tier 1 uses it to cancel a pull request's run when a newer commit replaces it.
- Give every job `timeout-minutes`.
- Pass `persist-credentials: false` to `actions/checkout`, so the job's token is not left in the checkout for later steps to read.
- Avoid the `pull_request_target` and `workflow_run` triggers. They run with the base repository's credentials, and a workflow that then checks out or builds a pull request's code gives that code those credentials.

A workflow that runs code from a pull request holds no secrets and a read-only token. Tier 1 is such a workflow: the `pull_request` trigger gives a pull request from a fork a read-only token and no secrets, whatever the workflow file requests.
