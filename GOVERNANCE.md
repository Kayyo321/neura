# Governance

This page states who decides what in the Neura repository, how a change reaches `main`, and who may publish a release. [CONTRIBUTING.md](CONTRIBUTING.md) covers how to prepare a change.

## Roles

| Role | Who | May |
| --- | --- | --- |
| Contributor | Anyone | Open issues and pull requests, review, and comment. |
| Maintainer | [@Kayyo321](https://github.com/Kayyo321) | Everything a contributor may, and: triage issues, approve and merge pull requests, change repository settings, publish releases and security advisories, and enforce the [code of conduct](CODE_OF_CONDUCT.md). |

A maintainer holds the GitHub `admin` role on the repository. No other account has write access. [`.github/CODEOWNERS`](.github/CODEOWNERS) names the maintainers whose review a change needs.

The project has one maintainer. A new maintainer is added by an existing maintainer, in a pull request that updates this page and `CODEOWNERS`, after a record of reviewed contributions to the project.

## How a change reaches `main`

1. Every change arrives as a pull request from a short-lived branch. Nobody pushes commits to `main` directly, rewrites its history, or deletes it.
2. The pull request's required checks pass on its final commit. A check that did not run counts as a failure.
3. A code owner approves the pull request. Pushing a new commit withdraws an earlier approval.
4. Every review conversation is resolved.
5. A maintainer merges the pull request.

### Review with one maintainer

GitHub does not let an author approve their own pull request, so step 3 cannot be met for a pull request the sole maintainer writes: no second person holds the role. Those pull requests are merged by their author once steps 1, 2, and 4 hold. A contributor's pull request is reviewed and approved by the maintainer as written above.

This is a limitation of the project's current size, and the rule returns to full force for every author when a second maintainer joins.

## Decisions

A maintainer decides whether a change is merged. A change to language behavior, a stage contract, or an artifact format is decided in the pull request that updates the owning specification, so the reasons are recorded with the change. An unresolved language question is recorded in [language status](docs/lang/Status.md) until it is decided.

Disagreement is settled by the maintainers. With one maintainer, that person's decision is final.

## Issue triage

A maintainer labels each new issue with its kind (`bug`, `enhancement`, `documentation`, or `question`) and closes it with a reason when it is a duplicate, cannot be reproduced from the report, or is declined. An issue that describes a vulnerability is closed without discussing its details in public, and its author is pointed to the [security policy](SECURITY.md).

## Releases

Only a maintainer creates a version tag or publishes a release. A release is built from a commit on `main`. A published tag is never moved or deleted, and a published release's files are never replaced; a mistake is corrected with a new version. [CHANGELOG.md](CHANGELOG.md) records what each version changes.

## Dependencies

Neura's build depends on the [`neura-libclang`](third_party/neura-libclang) submodule, on libffi, and on the C++ toolchain. Dependabot proposes updates to the submodule and to the GitHub Actions the workflows use ([`.github/dependabot.yml`](.github/dependabot.yml)). Each update is a pull request and goes through the steps above.

## Changing this page

A change to this page, to `CODEOWNERS`, or to the code of conduct is a pull request like any other and needs a maintainer's approval.
