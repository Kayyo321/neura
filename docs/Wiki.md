# Maintaining the GitHub wiki

The [published wiki](https://github.com/Kayyo321/neura/wiki) is a separate Git repository from Neura. Its publication sources are the Markdown files in [`wiki/`](../wiki/), which are reviewed and versioned with the project. The separate wiki repository has its own commit history; pushing the project branch does not update it.

The language and project specifications in `docs/` define the behavior and are the only place to edit them. Most wiki pages are generated from those documents by [`scripts/generate-wiki.py`](../scripts/generate-wiki.py): each starts with a comment naming its source, carries the wiki's navigation header under the title, and has its links rewritten for the wiki. A link to a docs page that has a wiki page becomes a bare wiki link (`Status#anchor`), and a link to any other file or directory of the repository becomes a `https://github.com/Kayyo321/neura/blob/main/...` (or `tree/main`) URL. Same-page and external links, code spans, and fenced blocks are unchanged. Never edit a generated page: the next run overwrites the edit, and `--check` fails until it is reverted.

The script holds two tables. `GENERATED` maps each docs page to its wiki page; `docs/lang/Stdlib.md` is published as `Stdlib-Design`, and the index of diagnostic codes, `docs/diagnostics/README.md`, as `Diagnostics`. `REPOSITORY_ONLY` lists the pages that have no wiki page: this one, the website's pages, and the design notes under `docs/design/`. The explain page of each diagnostic code, `docs/diagnostics/<CODE>.md`, has none either (`REPOSITORY_ONLY_PATTERNS`): the website publishes each one and `neura explain` prints it, and the wiki's `Diagnostics` index links to the pages in the repository. A comment before a docs page's title, such as the notice that `scripts/gen-kernels.py` writes at the top of `docs/runtime/Kernels.md`, is replaced by the wiki page's own. `HAND_WRITTEN` lists the wiki pages that are written by hand: `Home.md`, `_Sidebar.md`, `Neurons-and-Circuits.md`, and the `Stdlib.md` index. The `Stdlib-<Area>-<Module>.md` module pages are also written by hand, and `scripts/update-stdlib-wiki-api.py` refreshes their marked declaration blocks. There is no automatic publication to the separate wiki repository: maintainers must publish the generated sources deliberately, as below.

## Prepare an update

1. Update the owning specification in `docs/` when a language, compiler, or runtime contract changes. Record unresolved decisions and implementation status in [language status](lang/Status.md) where applicable.
2. Run `python3 scripts/generate-wiki.py` to rewrite the generated pages in `wiki/`, and include them in the same change. To add a docs page, add it to `GENERATED` (or to `REPOSITORY_ONLY` when it should not be on the wiki) and generate it. To add a wiki page that is not a mirror of a docs page, add it to `HAND_WRITTEN`. Update `Home.md` and `_Sidebar.md` by hand when navigation changes. The standard-library reference is split across the `Stdlib` index, which is written by hand, and the individual module pages; consult both `docs/lang/Stdlib.md` and `docs/lang/StandardLibrary.md`, as well as the module source, when updating those pages. Make sure descriptions state implemented behavior as fact and name open language decisions as open. Pages under `docs/design/` are repository-only design notes (`REPOSITORY_ONLY` in the script) and have no wiki page.
3. Review the changes, including links to wiki pages, section anchors, and repository files. Check examples and claims against the specifications and tests. Review the diff with `git diff -- docs wiki` and run the checks appropriate to any behavior change.
4. Commit the reviewed project changes before publishing. Use the resulting project commit ID in the separate wiki commit, so either history can be traced back to the source. Publish from the intended project revision, normally the revision on the repository's default branch; links to `main` only reflect changes once that branch has been pushed.

Run `python3 scripts/generate-wiki.py --check` to see whether a generated page is stale or missing, or whether `wiki/` holds a page that is neither generated nor listed as hand-written. The Tier 1 workflow runs it, so a pull request that changes a docs page without regenerating its wiki page fails.

For changes to public standard-library declarations or their adjacent source comments, run `python3 scripts/update-stdlib-wiki-api.py` to refresh the marked declaration blocks in the module pages. Run `python3 scripts/update-stdlib-wiki-api.py --check` to detect stale blocks, missing pages, or invalid markers. The notes show each declaration's `#Deprecated` and `#Since` tags and the tags that constrain callers, such as `#NoDiscard` and `#Final`. The script inventories public declarations and source comments; it does not write behavioral contracts, resolve inherited members, or update the index. A module page's "Detailed contracts" sections are copies of sections of `docs/lang/Stdlib.md` with the same headings; when a section of the specification changes, copy it into every module page that has it, with its links rewritten as `generate-wiki.py` rewrites them. Review and update those parts separately, especially when adding or removing a module.

The history of the repository sources is available with `git log -p -- wiki docs`. Wiki-only edits made on GitHub have a separate history and must be reconciled, in the owning `docs/` page for a generated page, rather than silently overwritten.

## Publish from any computer

These commands assume the current directory is the root of the Neura checkout and the project changes have been committed. Use a GitHub account with permission to edit the wiki. The clone is separate from the project checkout; it is not a submodule and should not be committed into this repository. No particular local path or previous agent's workspace is required.

Clone the latest wiki and inspect its history and its differences from the checked-out publication sources:

```sh
wiki_workspace="$(mktemp -d)"
git clone https://github.com/Kayyo321/neura.wiki.git "$wiki_workspace/wiki"
git -C "$wiki_workspace/wiki" log -5 --oneline
diff -ru --exclude=.git wiki "$wiki_workspace/wiki"
```

`diff` returns status 1 when files differ. Review those differences before copying: they can include both the intended project changes and edits made directly to the live wiki. Reconcile any remote-only edits into the owning `docs/` page (and then regenerate) or, for a hand-written page, into `wiki/`, review and commit them in the project repository, and repeat the comparison. Do not assume that a difference is safe to overwrite. If the wiki repository does not exist yet, a repository collaborator must first create and save an initial page through the GitHub Wiki tab; also check access and whether the wiki is enabled.

Once differences are understood and the project commit is ready, copy the publication sources into the wiki clone and review the exact changes:

```sh
cp wiki/*.md "$wiki_workspace/wiki/"
git -C "$wiki_workspace/wiki" status --short
git -C "$wiki_workspace/wiki" diff
```

This copies additions and updates; it does not delete pages. For an intentional rename or removal, inspect any old or wiki-only page first, reconcile content that should be preserved, then remove only the intended file in the wiki clone and verify the diff. Do not delete extra wiki files merely because they are absent locally.

When publishing is authorized, commit and push the reviewed changes. The source commit ID makes the publication auditable:

```sh
source_commit="$(git rev-parse HEAD)"
git -C "$wiki_workspace/wiki" add -A
git -C "$wiki_workspace/wiki" diff --cached
git -C "$wiki_workspace/wiki" commit -m "Update wiki from Neura $source_commit"
git -C "$wiki_workspace/wiki" push origin HEAD
```

If there are no changes, no wiki commit is needed. If the push is rejected because the wiki advanced, fetch and inspect the new history, reconcile those edits in the project sources, and repeat the review and publication. Do not force-push over another contributor's work.

After pushing, open the published wiki and check the changed pages, navigation, and links. Record the project and wiki commit IDs in the change report. The temporary clone may be removed after verification; all information needed to repeat the process is in the project and wiki repositories.
