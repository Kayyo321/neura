# Maintaining the website

The [Neura website](https://kayyo321.github.io/neura/) is published with GitHub Pages from this repository. It has two parts: the pages written for the site in [`site/`](../site/), and the repository's own Markdown documents, which the build renders into the site's documentation. A change to either is a pull request like any other: it is reviewed, checked by [Tier 1](ContinuousIntegration.md#tier-1-pull-request-checks), and published when it is merged into `main`. Nobody edits the published site directly, and there is no second branch or repository to keep in sync.

Every push to `main` rebuilds and redeploys the site from that commit, so the published documentation is always the documents on `main`: a change to a file in `wiki/`, `docs/`, or a Markdown file at the top of the checkout appears on the site a minute or two after it is merged, with no other step. A new document appears too, in the section `site/contents.toml` gives it or, until it has one, under "More documents". The site does not copy, summarize, or restate the documents.

The site renders the repository's `wiki/` directory, not the separately hosted GitHub wiki. An edit made only on GitHub's wiki pages reaches the site once it is reconciled into `wiki/` and merged, as [wiki maintenance](Wiki.md) describes. The language and project specifications in `docs/` define Neura's behavior; when a page of the site and a specification disagree, the specification is right and the site has a bug.

## What is where

| Path | Role |
| --- | --- |
| [`site/`](../site/) | The pages written for the site. Each file is published at the same path below the site's address: `site/index.html` is the front page, and `site/assets/site.css` is `assets/site.css`. |
| [`site/index.html`](../site/index.html) | The front page. Its text is included from the [README](../README.md) when the site is built. |
| [`site/404.html`](../site/404.html) | The page GitHub Pages returns for an address that names no file. |
| [`site/contents.toml`](../site/contents.toml) | The documents the site publishes, and the sections and order of the documentation's navigation. It is not published itself. |
| [`site/partials/`](../site/partials/) | The parts that every page shares: the head, the masthead, and the footer of the documentation. They are not published themselves. |
| [`site/assets/site.css`](../site/assets/site.css) | The one stylesheet, used by every page. |
| [`site/assets/docs.js`](../site/assets/docs.js) | The documentation's search box, the highlight of the current section, and the copy buttons on code. |
| [`site/assets/copy-prompt.js`](../site/assets/copy-prompt.js) | The front page's "Copy Neura Language Skill" controls, which copy the prompt that the page includes from the README's section [Write Neura with an AI agent](../README.md#write-neura-with-an-ai-agent). |
| [`site/assets/fonts/`](../site/assets/fonts/) | The font files and their licenses. |
| [`site/assets/plates/`](../site/assets/plates/) | The illustrations, drawn by [`artwork/site/plates.py`](../artwork/site/plates.py). |
| [`scripts/build-site.py`](../scripts/build-site.py) | Builds the site into a directory, checks it, and serves a preview. |
| [`scripts/site_docs.py`](../scripts/site_docs.py) | Expands partials and includes, and renders the documents listed in `contents.toml` into pages. |
| [`scripts/site_markdown.py`](../scripts/site_markdown.py) | Renders Markdown as GitHub does for the constructs the documents use. |
| [`.github/workflows/pages.yml`](../.github/workflows/pages.yml) | Builds the site from `main` and deploys it. |
| [The design of the website](SiteDesign.md) | The colors, the type, the grids, and the rules the illustrations follow. |

The site has no site generator, package manifest, or third-party code, and building it needs only Python 3.11 or newer and its standard library.

## How the documentation is published

[`site/contents.toml`](../site/contents.toml) lists the documents the site publishes, in sections. Each section is a group in the navigation and a shelf on the documentation's home page, the Reading Room at `docs/`. The build renders each listed document into a page with the documentation's layout: the navigation and a search box on the left, the document in the middle, and its sections on the right, with links to the previous and next document and to edit the document on GitHub.

A document is published at a path derived from its own:

| Document | Published at |
| --- | --- |
| `docs/P.md` | `docs/P.html` |
| A file at the top of the checkout, such as `CONTRIBUTING.md` | `docs/CONTRIBUTING.html` |
| `wiki/Stdlib.md` | `stdlib/index.html` |
| `wiki/Stdlib-X.md` | `stdlib/X.html` |
| Another `wiki/X.md` | `docs/X.html` |

The build rewrites each link in a document so that a reader stays on the site:

- A link to another published document leads to its page, and its `#fragment` to the same heading, because headings receive the `id` that GitHub gives them.
- A link to a page of the GitHub wiki, or a wiki page's short link such as `[Numbers](Numbers)`, leads to the published document with that name. `[wiki]` in `contents.toml` names the document for a wiki page whose name differs.
- A link to another text file of the repository, such as an example or a standard library module, leads to a page under `source/` that shows the file with numbered lines, so `#L21` reaches line 21.
- A link to a directory or an image of the repository, and a link to any other site, is left as it is.

The build also writes `assets/search.json`, the index that the search box reads: every page, every section of a page, and every exported member of the standard library.

### Add or move a document

A new Markdown file in `docs/`, in `wiki/`, or at the top of the checkout is published without a change to the site: `[unlisted]` in `site/contents.toml` matches it, and the build files it under "More documents", titled by its first heading. A new standard library module page, `wiki/Stdlib-Package-Module.md`, goes straight to its package: the `modules` pattern of the standard library section finds it. A wiki page that presents a published document under the same name is not published twice; a link to it leads to the document.

To give a document its place in the navigation:

1. Add an entry to the right section of `site/contents.toml`, with a `label` for the navigation when the document's first heading is long.
2. Preview the site and read the page.
3. Run the checks. A link from a published document to a Markdown file that the site does not publish, such as one in `exclude`, is a problem.

## Change the site

1. Read the [rules the build checks](#rules-the-build-checks), the [rules for content](#rules-for-content), and [the design of the website](SiteDesign.md).
2. Edit the files in `site/`, or the documents themselves. To add a page written for the site, add an HTML file, include the `head` and `masthead` partials as `site/index.html` does, and link to it from an existing page. Style it with the custom properties and classes that `site/assets/site.css` already has before adding new ones.
3. Preview the site and read every page you changed, in a wide window and at the width of a phone:

   ```sh
   python3 scripts/build-site.py --serve
   ```

   The preview is at `http://127.0.0.1:8000/neura/`. It serves the site below `/neura/` and answers a missing address with `404.html`, as GitHub Pages does. It rebuilds when a file in `site/`, `docs/`, `wiki/`, `examples/`, or `stdlib/`, or a Markdown file at the top of the checkout, changes, so reloading the page shows the edit. Restart it after changing a script in `scripts/`. It listens on your computer only. Problems the build finds are printed in the terminal.
4. Run the checks:

   ```sh
   python3 scripts/build-site.py --check
   python3 scripts/test-checks.py
   python3 scripts/check-docs.py
   ```

   The second command matters when you changed a script of the build, and the third when you changed a Markdown file.
5. Open a pull request. Say what a reader of the site sees differently, and attach a screenshot of a visual change at both widths. Name the specification page that supports each new statement about Neura.

A pull request is not deployed anywhere, so a reviewer previews it from a checkout of its branch with the command in step 3.

### Partials and includes

Because the site has no templates, the build replaces two kinds of comment in a page of `site/`:

- `<!-- include: masthead -->` is replaced by `site/partials/masthead.html`. In a partial, `{{root}}` becomes the path from the page to the top of the site (empty at the top, `../` one directory down), and `{{home}}` the same path or `./`. Change the masthead, the head, or the documentation's footer once, in its partial.
- `<!-- markdown: README.md#documentation -->` is replaced by that section of the document, rendered, heading and all. An empty fragment, as in `<!-- markdown: README.md# -->`, names the text before the document's first heading. The front page takes its words from the README this way. Add `without-code` after the fragment, as in `<!-- markdown: README.md#compilation-and-execution without-code -->`, to leave out the section's fenced code blocks and keep its prose; the front page leaves out the README's pipeline diagram this way.

## Rules the build checks

`scripts/build-site.py` reports each of these as `path:line: message` and exits with status 1. Tier 1 runs it on every pull request, and the Pages workflow runs it again before deploying. The check reads the site as it is published, after partials and includes are expanded and the documents are rendered. A problem in a rendered document names the document and the line of its link; a problem the HTML check finds in a rendered page names the document and the line of the published page.

- **Every reference to a file of the site resolves.** A link, image, stylesheet, script, or CSS `url(...)` names a published file with the same spelling and capitals. A reference to a directory needs an `index.html` in it. A `#fragment` needs an element with that `id` in the page it names.
- **No reference starts with `/`.** The site is served below `/neura/`, where `/assets/site.css` names a path outside the site. Write `assets/site.css` from a page at the top of the site and `../assets/site.css` from a page one directory down.
- **A page loads nothing from another host.** Images, stylesheets, scripts, fonts, and frames are files of the site. Showing a page then sends no request to a third party, the site keeps working when another host changes or disappears, and every byte a reader receives was reviewed in a pull request. A link that a reader follows (`<a href>`) may point anywhere.
- **Every link in a published document resolves.** It names a file of the repository, a published document, or a wiki page the site publishes, and its `#fragment` names a heading of the document it leads to. A link to a Markdown file that the site does not publish is a problem: add the document to `contents.toml`.
- **Every listed document, partial, and included section exists.**
- **No file of `site/` has the path of a page the build generates.**
- **Links into the repository name something that exists.** A link to `https://github.com/Kayyo321/neura/blob/main/<path>`, to `.../tree/main/<path>`, or to `.../wiki/<page>` names a path or a page in `wiki/` in the same checkout.
- **No page has a `<base>` element.** It would change what every relative reference in the page means.
- **A listing that names its source shows that file.** The text of `<pre data-source="examples/hello-world/hello-world.nra">` equals the file at that path, apart from white space at either end. Elements inside the listing, such as the `<span>` that colors a keyword, do not count as text.
- **No published file or directory name starts with `.`.** The deployment leaves such files out, so the source view publishes `.github/` as `_github/`.

The check reads `href`, `src`, `srcset`, and `poster` attributes, `<style>` elements, and CSS files. It does not run scripts, so it cannot see a request that a script makes; a reviewer checks any script by reading it. `assets/docs.js` requests only `assets/search.json`, and `assets/copy-prompt.js` requests nothing.

## Rules for content

A reviewer checks these. The build does not.

- **Let the documents speak.** The documentation is the repository's Markdown, rendered. Correct a document in the document, not on the site. Text written for the site, such as the labels of the navigation, names things and states no facts about Neura.
- **Say only what a specification supports.** Check each statement about the language, the toolchain, or the runtime against the page in `docs/` that owns it. [Feature status](lang/Status.md) separates supported behavior from proposals and open decisions, and the [release checklist](Release.md) maps each feature to its tests.
- **Copy Neura code from `examples/`.** Reproduce a file under [`examples/`](../examples/) exactly, name it in the `data-source` attribute of its `<pre>` so the build compares the two, and link to it.
- **Publish no measurement without its source.** A figure for speed, size, or coverage names the command and the commit that produced it.
- **Keep every page readable without scripts and by everyone.** The navigation and the Reading Room list every document, so the search box is a convenience. Give each page a `lang` attribute, a `<title>`, and one `<h1>`; give each image an `alt` text, empty when the image is decoration; keep headings in order; keep the keyboard focus visible; and keep text contrast at 4.5 to 1 or better. A page must be usable at a width of 320 CSS pixels.
- **Collect nothing about readers.** The site has no analytics, cookies, or forms.
- **Add only material the project may publish.** Third-party text, images, fonts, and code need a license that permits it and a note of their origin in the pull request, as [CONTRIBUTING.md](../CONTRIBUTING.md#submit-a-pull-request) requires. The site's own content is under the repository's [license](../LICENSE).

### Images and fonts

Every image of the site comes from [`artwork/site/plates.py`](../artwork/site/plates.py): the plates and the paper grain are drawn there, and the logo and the tab icon are made there from the project's logo, [`artwork/logo.png`](../artwork/logo.png). [The design of the website](SiteDesign.md#plates) lists them, states the rules they follow, and shows how to draw one. The script needs Pillow and pycairo, which the build does not; its output is committed.

The fonts in `site/assets/fonts/` are under the SIL Open Font License, and each family's license text is published beside it. [The design of the website](SiteDesign.md#type) names the files and what each is for.

### Versions of stylesheets, scripts, and images

GitHub Pages lets a browser keep any file for ten minutes without asking again. A page loaded after a deployment could then be shown with the previous stylesheet, which is how a fix can seem not to have arrived. The build therefore adds a version to every reference to a file of `assets/`, in pages and in stylesheets: the first ten characters of the SHA-256 of the file, as in `assets/site.css?v=c8665bf5dd`. A changed file has a new address, which the browser fetches, and an unchanged one keeps its address and its cached copy. Write references in `site/` without a version; the build adds it.

A page itself is still kept for up to ten minutes, so a reader who had the site open sees a change after that time or after reloading with the cache bypassed (Ctrl+Shift+R, or Cmd+Shift+R on a Mac).

### The page for a missing address

GitHub Pages returns `404.html` for an address that names no file, at the address that was requested. A relative reference in that page would resolve against a directory that does not exist. The build therefore inserts `<base href="/neura/">` into the published copy of `404.html`, which makes its references resolve from the top of the site. Write the references in `site/404.html` relative to the top of the site, as in any page there.

## How a change is published

[`pages.yml`](../.github/workflows/pages.yml) runs for every push to `main`, so the published site always corresponds to the newest commit on `main`. It has two jobs:

| Job | What it does | Token |
| --- | --- | --- |
| Build the site | Reads the Pages configuration for the path the site is served below, runs `scripts/build-site.py --output _site --base-path <that path>`, and uploads `_site` as the deployment artifact. A problem in the site fails the job, and nothing is deployed. | Reads the repository and the Pages configuration. |
| Deploy to GitHub Pages | Deploys the uploaded artifact to the `github-pages` environment. | Writes a Pages deployment, and requests the identity token that tells Pages which repository and branch the deployment comes from. |

The workflow does not run for pull requests, so code from a pull request never runs in a job that can deploy. [Continuous integration](ContinuousIntegration.md#pages-website-deployment) describes how the workflow follows the workflow rules.

To check a deployment, list the recent runs and then load the site. GitHub Pages lets browsers reuse a page for ten minutes, so an edit can take that long to appear in a browser that has the site open.

```sh
gh run list --workflow pages.yml --limit 5
```

A maintainer can run the workflow again without a new commit from the Actions tab or with `gh workflow run pages.yml --ref main`.

### Publish without waiting for Tier 1

Tier 1 builds and tests the whole compiler, which takes several minutes, and a change to the site cannot affect it. A maintainer with the repository's `admin` role can merge such a pull request before the checks finish:

```sh
gh pr merge <number> --admin --merge
```

The merge publishes the site as any merge does, and the Pages workflow still refuses to deploy a site that fails the check. [GOVERNANCE.md](../GOVERNANCE.md#merging-before-the-checks-finish) states who may do this and what they answer for. Run `python3 scripts/build-site.py --check` before merging this way.

To undo a change to the site, revert it in a new pull request. Merging the revert publishes the earlier site.

## Repository settings the site depends on

These settings are stored by GitHub, not in the source tree. Their expected values are kept with the repository's other settings in [`.github/settings`](../.github/settings), and [Repository settings](RepositorySettings.md) explains how a maintainer changes them and checks the live repository against the files.

| Setting | Value | Reason |
| --- | --- | --- |
| Pages, build and deployment source (`pages` in `repository.json`) | GitHub Actions | The workflow uploads the built site. No branch is published, and GitHub's Jekyll build does not run. |
| Pages, HTTPS (`pages`) | Enforced | Readers are sent to the `https` address. |
| Environment `github-pages`, deployment branches (`environment-github-pages.json` and `repository.json`) | `main` only | GitHub refuses a deployment that any other branch requests, including one from a manual run or from a changed copy of the workflow on that branch. |

Check the live values with:

```sh
python3 scripts/check-repository-settings.py
```

A maintainer set them up once, in this order. Turning Pages on with the workflow as its source creates the `github-pages` environment and limits it to the default branch; the second command applies the environment's file, and the third lists its branches, which must be `main` alone:

```sh
gh api -X POST repos/Kayyo321/neura/pages -f build_type=workflow
gh api -X PUT repos/Kayyo321/neura/environments/github-pages --input .github/settings/environment-github-pages.json
gh api repos/Kayyo321/neura/environments/github-pages/deployment-branch-policies --jq '.branch_policies[].name'
```

If the list is empty, add the branch with `gh api -X POST repos/Kayyo321/neura/environments/github-pages/deployment-branch-policies -f name=main -f type=branch`.

## Change how the site is built

The deployment depends on one command: `python3 scripts/build-site.py --output DIR --base-path PATH` writes the complete site into `DIR`, and exits with a status other than 0 when the site must not be published. Keep that contract when the build changes, so the workflow, the preview, and Tier 1 keep working. Cover a change to the build or the Markdown renderer with a case in [`scripts/test-checks.py`](../scripts/test-checks.py).

The Markdown renderer covers the constructs the documents use, listed at the top of `site_markdown.py`. When a document needs another one, extend the renderer and add a case, rather than working around it in the document.

Before adding a site generator, a package, or a script from another project, open an issue that states what the site needs and cannot do today. Such a change also:

- pins every tool and package to an exact version and checksum, and adds its ecosystem to [`.github/dependabot.yml`](../.github/dependabot.yml);
- keeps the [rules the build checks](#rules-the-build-checks), applied to the files that are published;
- keeps the build independent of the network once its pinned tools are installed, and its output the same for the same commit;
- follows the [workflow rules](ContinuousIntegration.md#workflow-rules); and
- updates this page, [Continuous integration](ContinuousIntegration.md), and the instruction in [AGENTS.md](../AGENTS.md).

## The site's address

The address is `https://kayyo321.github.io/neura/`: the account that owns the repository, then the repository's name. No file in `site/` depends on it, apart from comments. The build takes the path from the Pages configuration when it deploys and uses `/neura` otherwise.

Moving the site to a domain of its own is a maintainer's decision. It is configured under Settings, Pages, Custom domain, and needs no file in `site/`. The same pull request then updates the address in this page, in the [README](../README.md), and in the comments in `site/`.

## When something goes wrong

| Symptom | Cause and fix |
| --- | --- |
| The published page has no styles or images, and the preview looks right. | A reference starts with `/` or differs from the file's name in capitals. The check reports both; run it. |
| The check reports that a document links to a file the site does not publish. | The file is outside the `[unlisted]` patterns or in `exclude`. Add it to a section of `site/contents.toml`, or link to a published document. |
| A merged change to a document is not on the site. | The Pages run failed, most often on a problem the check reports, and the site keeps its previous version. Read the run with `gh run list --workflow pages.yml`, fix the problem in a pull request, and merge it. Tier 1 runs the same check on every pull request, so this follows only a push that skipped review. |
| A document shows Markdown syntax as text on the site, and renders on GitHub. | The renderer lacks that construct. Extend `scripts/site_markdown.py` and add a case to `scripts/test-checks.py`. |
| The build job fails in "Read the Pages configuration". | Pages is turned off, or its source is not GitHub Actions. See [the settings](#repository-settings-the-site-depends-on). |
| The deploy job is refused by the environment's protection rules. | The run was started from a branch other than `main`. Merge the change, or run the workflow for `main`. |
| The site shows the previous version after a merge. | The run has not finished, or the browser is reusing its copy. Check the run, wait ten minutes, and reload. |
| The preview does not show a change to a script of the build. | The preview loads the scripts once. Stop it and start it again. |
