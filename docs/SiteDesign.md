# The design of the website

This page records the visual decisions behind the [Neura website](https://kayyo321.github.io/neura/), so that a change to the site continues the design instead of replacing it by accident. [Website maintenance](Site.md) covers how the site is changed, checked, and published. Read both before editing a file in [`site/`](../site/).

## The idea

The site looks like an illustrated research journal crossed with an early computer archive, seen after dark. Its palette comes from the Nissl stain, cresyl violet, which colors the cell body of every neuron in a slice so that the tissue reads as a field of violet points on black. The site splits that violet into the two inks it is mixed from, a pink and a blue, and lets the eye mix them back: every plate counts its marks out in both, side by side, in an ordered dither.

Three decisions follow from that, and every page keeps them:

- **A dark ground, a pale reading color, and an ink of two hues.** Pink is the ink of text and controls; blue appears only inside plates and as a marker, dithered against the pink.
- **Three voices of type.** Tall narrow headlines, wide reading text, and condensed capitals for labels.
- **Plates made of counted marks.** Every illustration is built from dots, lines, or square pixels, with one way of counting per plate, and finished with a visible ordered dither.

The site has two registers. The front page is the printed sheet: paper grain, a frame of ink around the viewport, bands of dither at its edges, plates across the canvas, and a closing poster. The documentation is the reference room: the same colors and typefaces, without grain, frame, or bands, set for reading and for finding things quickly. Both hold large areas of near black and spend the ink in a few decisive places: the primary action, the current place in the navigation, the plates, and the poster.

## Colors

The colors are custom properties at the top of [`site/assets/site.css`](../site/assets/site.css). [`artwork/site/plates.py`](../artwork/site/plates.py) holds the same values for the plates (`GROUND`, `PALE`, `INK`, `BLUE`). Change both together, and draw the plates again.

| Property | Value | Role | Contrast |
| --- | --- | --- | --- |
| `--ground` | `#09070e` | The page, and the darkest tone of every plate. | |
| `--pale` | `#e8e0ee` | Reading text, headlines, the logo, and the highlights of a plate. | 15.6 to 1 on the ground |
| `--ink` | `#d94c8a` | The frame, the primary action, links, code keywords, and the plates. | 5.1 to 1 on the ground |
| `--blue` | `#4a5ee0` | The plates, dithered with the ink; the current section of a page, and the edge of a quotation. Never text. | 3.8 to 1 on the ground |
| `--raised` | `#121017` | Code listings, the search box, and its results. | |
| `--rule` | `#2d2a32` | Fine dividing lines. Never text. | |
| `--quiet` | `#98929d` | Labels, captions, and secondary text. | 6.6 to 1 on the ground |
| `--wordmark` | `#1a171f` | The wordmark behind the closing poster's links. | |

A button is ground on ink, at 5.1 to 1. Strings and numbers in code mix the reading color with blue. The site has one color scheme. A page declares `color-scheme: dark`, and nothing responds to the reader's light or dark preference.

Add no other hue. A new tone is a mixture of these, and text in it reaches 4.5 to 1 against what is behind it.

### The light theme

A reader whose system or browser asks for a light theme gets a blueprint instead of the stain: paper `#f2f1ee`, dark blue text `#0f1460` (14.4 to 1), a blue ink `#1f29c9` for links and controls (8.4 to 1), the logo's own slate `#768092` as the second hue and the logo's color, and quiet text `#4a5170` (6.9 to 1). The plates have light versions in `site/assets/plates/light/`, drawn by the same script with a dark gritty blue `#161bb4` as their darkest tone and the ink dithered between slate and a bright blue `#4f63ff`; each page offers them with `<picture>` and `prefers-color-scheme`. On the front page the field around the canvas is that blue, broken by an ordered dither (`grit-light.png`), the canvas is paper, and the closing poster is the blue panel. The custom properties are redefined in one `@media (prefers-color-scheme: light)` block at the top of the stylesheet, and the front page's light rules sit at its end. There is no switch on the page: the site follows the reader's setting.

## Type

The fonts are files in [`site/assets/fonts/`](../site/assets/fonts/), each under the SIL Open Font License 1.1, with its license text beside it. No font is loaded from another host.

| Voice | Face | File | Used for |
| --- | --- | --- | --- |
| Display | Big Shoulders Regular | `BigShoulders-Regular.ttf` (92 KB) | Headlines, in mixed case, set tight and tall. |
| Label | Big Shoulders Bold | `BigShoulders-Bold.ttf` (92 KB) | Navigation, categories, captions, and controls: 13 px capitals, spaced by 0.09 em. |
| Reading | Work Sans Regular | `WorkSans-Regular.ttf` (184 KB) | Paragraphs, at 17.5 px on a 28 px line. |
| Code | Red Hat Mono Regular | `RedHatMono-Regular.ttf` (34 KB) | Code, commands, and the truth table. Never labels. |

Big Shoulders is by The Big Shoulders Project Authors, Work Sans by The Work Sans Project Authors, and Red Hat Mono by The Red Hat Project Authors. The files are the upstream TrueType files, unchanged. They are not subset or compressed, which costs about 400 KB on a first visit; converting them to WOFF2 subsets is an improvement that waits for a tool to do it reproducibly.

Work Sans ships in one weight. Reading text has no bold: emphasis comes from the label voice or from the ink color.

Scale changes in jumps. A label is 13 px beside a headline of 46 px or more. The front page names the project, "Neura", at up to 11.5 rem, and has no slogan: its words are the README's. The wordmark of the closing poster is set as wide as the reading grid: "NEURA" is 2.083 em wide in Big Shoulders Regular, and the rule `font-size: calc(var(--grid) / 2.083)` sets it across the grid. The divisor is a property of the word and the face.

In the documentation, the page title and its sections are set in the display face, and smaller headings in the reading face, so that a long reference keeps a calm hierarchy.

## Grids

The front page nests two widths, both custom properties: `--canvas`, 1440 px, which plates and the poster fill, and `--grid`, 1080 px, which holds everything a reader reads. At a 1920 px viewport that leaves broad dark margins outside the canvas.

| Part | Composition |
| --- | --- |
| Masthead | Four quiet links, the logo on the centre axis, and four more links, the last of them the one filled action. Both groups keep the same gap and lean on the logo, so the row balances around the middle of the page. A fine rule closes it. Below 72 rem the links fold into a `<details>` menu beside the logo. The masthead is the partial `site/partials/masthead.html`, shared by every page. |
| Opening | A plate across the whole canvas, then the name in five columns beside the README's opening paragraph and the actions in six. |
| Spreads | Twelve columns: a plate in six, and a section of the README in the other six, alternating sides. Below 54 rem they become one column, because twelve columns and their gaps are wider than a phone. |
| Closing poster | Described [below](#the-closing-poster). |

## The documentation

Every page of the documentation, generated by [`scripts/site_docs.py`](../scripts/site_docs.py), has three parts, so that a reader always sees where they are and how to move on:

| Part | Composition |
| --- | --- |
| Masthead | The shared masthead, compact, and fixed to the top while the page scrolls. |
| Directory | 17.5 rem on the left: the search box, then every section of the documentation as a disclosure, with the current section open and the current page marked in ink. The standard library's modules are grouped by package. It scrolls on its own. |
| Page | Up to 47 rem of reading: the path to the page, the document, where it came from with a link to edit it, and links to the previous and next document. |
| Contents | 15 rem on the right: the page's sections and subsections. The one being read is marked in ink while the page scrolls. |

Below 80 rem the contents column is left out; a heading's own link still gives its address. Below 54 rem the page is one column: the search box, a link to the list of documents, the page, and then the directory.

The search box opens on `/` or Ctrl+K and reads `assets/search.json` the first time it is used. Results are ranked by how closely they match: a whole name, then a name's start, then a word's start, then any part, and pages before sections before library members. The arrow keys choose a result and Enter opens it.

The Reading Room, `docs/index.html`, is the documentation's home: its name, a large search box, and a shelf for each section in two columns, each headed by its plate and listing each document with its first sentence.

## Plates

A plate is an illustration. The plates are PNG files in [`site/assets/plates/`](../site/assets/plates/), drawn by [`artwork/site/plates.py`](../artwork/site/plates.py) and committed.

### Rules

- **One subject and one second layer.** The subject is recognizable at the width of a phone. The second layer is thinner and more regular: a circuit, a reticle, a row of ticks, a dimension line. The subject says something about the text beside it.
- **A deliberate crop.** Enlarge the subject until part of it leaves the frame, or bring it in from a corner.
- **One treatment, then the dither.** Tone is counted out in marks of ink and pale on the ground, or of ground and pale on ink. A plate uses one of `halftone`, `lines`, `stipple`, and `bitmap`. When it is saved, `finish` replaces its ink, cell by cell of an 8 by 8 Bayer matrix, with pink or blue, blue growing toward the upper right with a slow wander, and lets the ink sink into the ground in the same ordered pattern away from the plate's light. The cells are three or four pixels, so the dither stays visible at the size a page shows the plate. Shapes that must stay sharp, such as letters and graduations, are laid over the marks.
- **Two kinds of edge.** A plate either ends at a clean rectangle, like a page tipped into a book, or has no ground of its own and is printed on the page.
- **Original or owned sources.** Subjects are drawn in code, or come from the project's own artwork in [`artwork/`](../artwork/). A photograph or drawing from elsewhere needs a license that permits it, and a note of its origin here.
- **A family of motifs.** The eye, the stained neuron, and the lattice of a circuit return at different sizes. Add a new motif only when none of these can carry the subject.

### The plates

| File | Subject and second layer | Treatment | Used |
| --- | --- | --- | --- |
| `neuron-hero.png` | A stained neuron in a lit field, its branches running on as pale wires into a layered circuit that glows out of the dark. | Dot screen on ink | Front page, opening |
| `neuron-slide.png` | The same, cropped closer. | Dot screen on ink | Front page, beside compilation and execution, and the opening on a phone; Reading Room, compiler and circuits |
| `type-tree.png` | Three lines of metal type, with the syntax tree of the statement. | Dot screen | Front page, beside the documentation; Reading Room, language |
| `caliper.png` | A vernier caliper closed on one node, with a dimension line. | Engraved lines | Reading Room, toolchain |
| `tape.png` | Two tape reels, with a tick for each record. | Bitmap | Reading Room, runtime |
| `balance.png` | A level balance: weights in one pan, a circuit in the other. | Stipple | Reading Room, contributing and governance |
| `eye-scan.png` | The eye, with a measuring reticle on the iris. | Scanlines | Decompiler page |
| `eye-poster.png` | The eye at the scale of a poster, thinning out into the dark. | Dot screen | Closing poster |
| `eye-iris.png` | The iris under its lid. | Coarse dot screen | Reading Room, start here |
| `catalogue.png` | Drawers of a card catalogue, one drawn out. | Engraved lines | Reading Room, standard library; standard library reference |
| `cores.png` | A core memory plane, with a few cores lit. | Bitmap | Reading Room, project records; missing page |
| `neuron-engraving.png` | A pale neuron in an aura of engraved lines, with no ground. | Engraved lines | Neurons and circuits page |

The eye plates are made from [`artwork/logo-source.png`](../artwork/logo-source.png), the project's own drawing. The type in `type-tree.png` is the first line of [`examples/hello-world/hello-world.nra`](../examples/hello-world/hello-world.nra), set mirrored as metal type is.

### Draw a plate

The script needs Python 3 with Pillow and pycairo. They are tools for drawing, and the site's build does not use them.

```sh
python3 artwork/site/plates.py                  # every plate, the logo, and the grain
python3 artwork/site/plates.py caliper tape     # the named ones
```

A plate is two steps in the script: a subject function draws a tone image from 0 (dark) to 1 (light), and one treatment turns the tone into marks. To add a plate, write a subject function, add it to `PLATES`, draw it, look at the result at the size the page shows it, and add a row to the table above. The drawing is seeded, so the same script draws the same plate. Commit the script and the PNG together.

Give each plate in a page its `width` and `height`, a `loading="lazy"` attribute when it is below the opening, and an `alt` text that says what the plate shows. A plate that only repeats a motif, as the poster's eye does, takes an empty `alt`.

A plate reaches the documentation through `site/contents.toml`: `plate` on a section heads its shelf in the Reading Room, and `plate` on a page shows a strip of the plate above the document. Give a page a plate only when the plate shows the page's subject.

## The logo

The logo is the project's own drawing, [`artwork/logo.png`](../artwork/logo.png): a detailed eye in slate lines on a transparent ground, with white light in the iris. It is drawn for a light page, and its slate nearly vanishes on the site's ground, so the site shows the same drawing reversed, as a printer reverses a mark out of a dark field: every line keeps its shape, and its darkness becomes its opacity.

`plates.py logo` writes that opacity as `site/assets/logo-mask.png`, three times the largest size a page shows it, and the tab icon `icon.png`. The stylesheet paints the mask with `mask-image` in a color of the palette, so the logo follows any change of palette without being drawn again: the reading color in the masthead and the documentation's footer, ink when pointed at, and the reading color again on the closing poster. Its size is the custom property `--logo`: 5.75 rem in the masthead, which is the same height on every page, 5 rem on a phone, and 11 rem on the poster.

## The closing poster

The front page ends in a poster across the full canvas, opened by a band of dither. It takes one motif, the eye, and shows it far larger than anywhere else, in pink and blue dots with pale light on the lashes. The upper part keeps its detail, and the dots thin out toward the bottom of the image, where the dark of the panel takes over. Behind the links, the wordmark is set in `--wordmark` across the grid, barely lighter than the ground. The links stay sharp and pale in front of it. The first column holds the logo; four narrow groups of links follow, and a last row holds the license and the link to this documentation.

On a phone the image is cropped to the iris and the sweep of the lashes.

## Controls and states

- A button is a compact filled rectangle with 2 px corners and a label of one to three words. Pointing at it changes its color and nothing else, so nothing moves.
- A link in text is underlined in ink, and turns ink when pointed at.
- Keyboard focus is a 2 px outline, in ink on the ground and in the ground color on the poster.
- Selected text is ground on ink.
- The site has no animation. Smooth scrolling to a section applies only when the reader has not asked for reduced motion.

## Labels and numbers

A label names a category that the reader uses: a section of the navigation, or the path to the current page. Number a group only when it is a sequence.

## Words

The site writes no slogans. Its words about Neura are the repository's documents, included or rendered when the site is built; the site's own words are names: of links, sections, and controls. The [rules for content](Site.md#rules-for-content) decide what the site may say.

## Bands of dither

`band.png`, drawn by `plates.py band`, is a strip of ordered dither, pink into blue along its length and thinning to nothing toward its top edge. The front page repeats it across the top of the opening plate, below the masthead, and across the top of the closing poster, so that the page's own rules are made of the same counted marks as its plates. The documentation keeps plain rules.
