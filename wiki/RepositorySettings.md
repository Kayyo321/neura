<!-- Generated from docs/RepositorySettings.md by scripts/generate-wiki.py. Edit that file, not this one. -->
# Repository settings

[Wiki home](https://github.com/Kayyo321/neura/wiki) · [Language overview](https://github.com/Kayyo321/neura/wiki/Overview) · [Feature status and open decisions](https://github.com/Kayyo321/neura/wiki/Status)

The rules in [GOVERNANCE.md](https://github.com/Kayyo321/neura/blob/main/GOVERNANCE.md) are enforced by settings of the GitHub repository. Settings live outside the source tree, so the expected values are kept as files in [`.github/settings`](https://github.com/Kayyo321/neura/tree/main/.github/settings) and a script compares them with the live repository.

## What is enforced

| Setting | File | Effect |
| --- | --- | --- |
| Ruleset `main` | `ruleset-main.json` | A change reaches `main` only through a pull request whose **Tier 1** check passed and whose review conversations are resolved. Nobody can force-push to `main` or delete it. No account can bypass the ruleset, the maintainer included. |
| Ruleset `release tags: creation` | `ruleset-release-tags-create.json` | Only an account with the `admin` role can create a tag that matches `v*`. |
| Ruleset `release tags: no move or delete` | `ruleset-release-tags-protect.json` | A `v*` tag cannot be moved or deleted. No account can bypass this, so a mistaken tag stays and the correction is a new version. |
| Actions policy | `repository.json` | Workflows can use only actions that GitHub publishes, each pinned to a full commit hash. The default workflow token is read-only and cannot approve pull requests. |
| Runs from outside contributors | `repository.json` | A maintainer approves each workflow run for a pull request whose author has no write access. |
| Security features | `repository.json` | Secret scanning with push protection, Dependabot alerts and security updates, and CodeQL code scanning with GitHub's default setup. |
| Environment `release` | `repository.json`, `environment-release.json` | A job that names this environment starts only from a `v*` tag and only after the maintainer approves it. Signing and publishing secrets belong here and nowhere else. |
| Immutable releases | `repository.json` | A published release's tag and files cannot be changed. |
| Merged branches | `repository.json` | GitHub deletes a pull request's branch when it merges. |

## Review with one maintainer

`ruleset-main.json` requires a pull request and requires zero approving reviews. GitHub does not let an author approve their own pull request, and the project has one maintainer, so a rule that required a code owner's approval would stop every pull request the maintainer writes. [GOVERNANCE.md](https://github.com/Kayyo321/neura/blob/main/GOVERNANCE.md#review-with-one-maintainer) records this limit.

The consequence is that GitHub does not enforce the maintainer's review of a contributor's pull request either: that review is a rule the maintainer follows, and only a maintainer can merge. When a second maintainer joins, set `required_approving_review_count` to 1 and `require_code_owner_review` to `true` in `ruleset-main.json`, and apply the change.

## Check the live settings

```sh
python3 scripts/check-repository-settings.py
```

The script reads each setting with the `gh` command, which must be signed in to an account that can read the repository's settings, and prints each value that differs from the files. It exits with status 1 when anything differs, when a ruleset exists on GitHub without a file, or when a setting cannot be read. Tier 1 does not run it: a pull request's token cannot read these settings. A maintainer runs it after changing a setting and before each release.

## Change a setting

1. Change the file in a pull request, with the reason.
2. After it merges, apply the same change to the repository. For a ruleset, send the file to the API:

   ```sh
   gh api repos/Kayyo321/neura/rulesets --jq '.[] | "\(.id) \(.name)"'
   gh api -X PUT repos/Kayyo321/neura/rulesets/<id> --input .github/settings/ruleset-main.json
   ```

   Other settings are changed on the repository's **Settings** pages or with the API path that is the key in `repository.json`.
3. Run the check script and confirm it reports no problems.
