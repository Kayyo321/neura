#!/usr/bin/env python3
"""Draw the plates of the Neura website.

A plate is an illustration built from counted marks in the site's three
colors. Each one is drawn in two steps:

  1. A subject function draws a tone image, from 0 (dark) to 1 (light), with
     vector shapes and gradients. The eye plates start from the project's own
     artwork, `artwork/logo-source.png`.
  2. One treatment turns the tone into marks: `halftone` (a rotated screen of
     dots), `lines` (engraved lines that swell with the tone; at angle 0 they
     are scanlines), `stipple` (dots placed by error diffusion), or `bitmap`
     (coarse square pixels placed by Atkinson dithering).

docs/SiteDesign.md describes the rules the plates follow. Run this script from
the repository root to draw every plate again, or name the plates to draw:

    python3 artwork/site/plates.py                      # both themes
    python3 artwork/site/plates.py neuron-slide eye-scan
    python3 artwork/site/plates.py --light caliper      # one theme

The script needs Pillow and pycairo. It is not part of the site's build: the
PNG files it writes into `site/assets/plates/` are committed. The drawing is
seeded, so the same script draws the same plates.
"""

from __future__ import annotations

import math
import random
import sys
from pathlib import Path

import cairo
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "site" / "assets"
PLATES_DIR = ASSETS / "plates"
# Where plates are written: PLATES_DIR for the dark theme, a directory in it for another (see `use_theme`).
OUT = PLATES_DIR
FONTS = ROOT / "site" / "assets" / "fonts"
EYE = ROOT / "artwork" / "logo-source.png"

# The site's colors (docs/SiteDesign.md). Keep them equal to the custom
# properties in site/assets/site.css. The subjects are drawn in GROUND, PALE,
# and INK; when a plate is saved, its ink is dithered between INK (pink) and
# BLUE, so the two read together as the violet of a Nissl stain.
#
# The site has a dark and a light theme, and each plate is drawn in both. The
# dark plates go to site/assets/plates/, the light ones to plates/light/.
THEMES = {
    # A Nissl stain: violet-black, with ink dithered between pink and blue.
    "dark": {"GROUND": (0x09, 0x07, 0x0E), "INK": (0xD9, 0x4C, 0x8A), "BLUE": (0x4A, 0x5E, 0xE0),
             "PALE": (0xE8, 0xE0, 0xEE)},
    # A blueprint: a dark gritty blue, with ink dithered between the logo's slate and a bright blue, on paper.
    "light": {"GROUND": (0x16, 0x1B, 0xB4), "INK": (0x76, 0x80, 0x92), "BLUE": (0x4F, 0x63, 0xFF),
              "PALE": (0xF2, 0xF1, 0xEE)},
}
THEME = "dark"
GROUND, INK, BLUE, PALE = (THEMES[THEME][key] for key in ("GROUND", "INK", "BLUE", "PALE"))

# An 8 by 8 Bayer matrix: the thresholds of ordered dithering, from 0 to 1.
BAYER = [[(v + 0.5) / 64 for v in row] for row in (
    (0, 32, 8, 40, 2, 34, 10, 42), (48, 16, 56, 24, 50, 18, 58, 26),
    (12, 44, 4, 36, 14, 46, 6, 38), (60, 28, 52, 20, 62, 30, 54, 22),
    (3, 35, 11, 43, 1, 33, 9, 41), (51, 19, 59, 27, 49, 17, 57, 25),
    (15, 47, 7, 39, 13, 45, 5, 37), (63, 31, 55, 23, 61, 29, 53, 21),
)]

TAU = 2 * math.pi


def unit(color: tuple[int, int, int]) -> tuple[float, float, float]:
    return color[0] / 255, color[1] / 255, color[2] / 255


def clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return low if value < low else high if value > high else value


def smooth(edge0: float, edge1: float, value: float) -> float:
    t = clamp((value - edge0) / (edge1 - edge0))
    return t * t * (3 - 2 * t)


# ----------------------------------------------------------------------------
# Tone: a grayscale drawing that the treatments sample.


class Canvas:
    """A cairo surface for drawing tone. `gray(0)` is dark and `gray(1)` is light."""

    def __init__(self, width: int, height: int, tone: float = 0.0) -> None:
        self.width, self.height = width, height
        self.surface = cairo.ImageSurface(cairo.FORMAT_RGB24, width, height)
        self.c = cairo.Context(self.surface)
        self.c.set_line_cap(cairo.LINE_CAP_ROUND)
        self.c.set_line_join(cairo.LINE_JOIN_ROUND)
        self.gray(tone)
        self.c.paint()

    def gray(self, tone: float, alpha: float = 1.0) -> None:
        self.c.set_source_rgba(tone, tone, tone, alpha)

    def linear(self, x0: float, y0: float, x1: float, y1: float, stops: list[tuple[float, float, float]]) -> None:
        """Set a linear gradient source. Each stop is (offset, tone, alpha)."""
        gradient = cairo.LinearGradient(x0, y0, x1, y1)
        for offset, tone, alpha in stops:
            gradient.add_color_stop_rgba(offset, tone, tone, tone, alpha)
        self.c.set_source(gradient)

    def radial(self, x: float, y: float, r0: float, r1: float, stops: list[tuple[float, float, float]]) -> None:
        """Set a radial gradient source. Each stop is (offset, tone, alpha)."""
        gradient = cairo.RadialGradient(x, y, r0, x, y, r1)
        for offset, tone, alpha in stops:
            gradient.add_color_stop_rgba(offset, tone, tone, tone, alpha)
        self.c.set_source(gradient)

    def image(self) -> Image.Image:
        self.surface.flush()
        data = bytes(self.surface.get_data())
        return Image.frombuffer("RGBA", (self.width, self.height), data, "raw", "BGRA",
                                self.surface.get_stride(), 1).convert("L")


class Tone:
    """Samples a tone image at any point, after an optional blur."""

    def __init__(self, image: Image.Image, blur: float = 0.0) -> None:
        self.image = image.filter(ImageFilter.GaussianBlur(blur)) if blur else image
        self.width, self.height = image.size
        self.pixels = self.image.load()

    def at(self, x: float, y: float) -> float:
        xi = min(max(int(x), 0), self.width - 1)
        yi = min(max(int(y), 0), self.height - 1)
        return self.pixels[xi, yi] / 255


def linearize(image: Image.Image) -> Image.Image:
    """Convert an sRGB grayscale image to linear light, so tone is proportional to brightness."""
    table = []
    for value in range(256):
        c = value / 255
        table.append(round(255 * (c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)))
    return image.point(table)


# ----------------------------------------------------------------------------
# Plate: the finished picture, drawn in the site's colors.


class Plate:
    def __init__(self, width: int, height: int, ground: tuple[int, int, int] | None | str = "ground") -> None:
        """A plate on a `ground` color (the theme's GROUND by default), or on nothing when `ground` is None."""
        if ground == "ground":
            ground = GROUND
        self.width, self.height = width, height
        self.surface = cairo.ImageSurface(cairo.FORMAT_ARGB32, width, height)
        self.c = cairo.Context(self.surface)
        self.c.set_line_cap(cairo.LINE_CAP_ROUND)
        self.c.set_line_join(cairo.LINE_JOIN_ROUND)
        if ground is not None:
            self.c.set_source_rgb(*unit(ground))
            self.c.paint()
        self.stamps: list[tuple[Image.Image, tuple[int, int, int]]] = []

    def stamp(self, mask: Image.Image, color: tuple[int, int, int]) -> None:
        """Lay a flat color through `mask` over the finished marks, for shapes that must stay crisp."""
        self.stamps.append((mask, color))

    def color(self, color: tuple[int, int, int], alpha: float = 1.0) -> None:
        self.c.set_source_rgba(*unit(color), alpha)

    def image(self) -> Image.Image:
        self.surface.flush()
        data = bytes(self.surface.get_data())
        # Cairo stores premultiplied BGRA.
        return Image.frombuffer("RGBA", (self.width, self.height), data, "raw", "BGRa", self.surface.get_stride(), 1)

    def save(self, name: str, colors: int = 32, cell: int = 4, light: tuple[float, float] | None = None,
             fall: float = 0.35, floor: float = 0.0, seed: int = 1) -> Path:
        """Write the plate as an indexed PNG, after `finish`. A few colors keep the antialiased edges."""
        image = self.image()
        for mask, color in self.stamps:
            image.paste(color + (255,), mask=mask)
        image = finish(image, cell, light, fall, floor, seed)
        if image.getextrema()[3][0] == 255:
            image = image.convert("RGB").quantize(colors, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        else:
            image = image.quantize(colors * 2, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
        OUT.mkdir(parents=True, exist_ok=True)
        path = OUT / f"{name}.png"
        image.save(path, optimize=True)
        print(f"{path.relative_to(ROOT)}  {self.width}x{self.height}  {path.stat().st_size // 1024} KB")
        return path


def finish(image: Image.Image, cell: int, light: tuple[float, float] | None, fall: float, floor: float,
           seed: int) -> Image.Image:
    """Dither a plate's ink between pink and blue, and let it sink into the ground away from the light.

    Every pixel is a mix of GROUND, PALE, and INK. Its ink part is replaced, cell by cell of an ordered (Bayer)
    dither, by BLUE or by INK: blue grows toward the upper right, with a slow wander so that no plate shows a plain
    ramp. A second dither, offset from the first, turns ink to ground with a probability that rises with the
    distance from `light` (the centre when None), from `floor` at the light to `fall` far from it, so a plate is
    darkest at its edges. The cells are
    large enough, `cell` pixels, to stay visible at the size a page shows the plate.
    """
    width, height = image.size
    rng = random.Random(seed)
    # A coarse field of random values, smoothed when it is enlarged: the wander of the pink and the blue.
    wander = Image.new("L", (6, 4))
    wander.putdata([rng.randrange(256) for _ in range(24)])
    wander = wander.resize((width, height), Image.BICUBIC).load()
    lx, ly = light if light else (width / 2, height / 2)
    reach = math.hypot(max(lx, width - lx), max(ly, height - ly))

    g, p, k = GROUND, PALE, INK
    # Solve pixel = a*GROUND + b*PALE + c*INK for (a, b, c) by Cramer's rule, cached per color.
    matrix = [[g[i], p[i], k[i]] for i in range(3)]

    def det(m):
        return (m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
                + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]))

    whole = det(matrix)
    cache: dict = {}

    def weights(color):
        found = cache.get(color)
        if found is None:
            found = []
            for column in range(3):
                m = [row[:] for row in matrix]
                for i in range(3):
                    m[i][column] = color[i]
                found.append(det(m) / whole)
            cache[color] = found
        return found

    pixels = image.load()
    for y in range(height):
        row_a, row_b = BAYER[(y // cell) % 8], BAYER[(y // cell + 3) % 8]
        for x in range(width):
            r, gg, b, a = pixels[x, y]
            if not a:
                continue
            wg, wp, wk = weights((r, gg, b))
            if wk < 0.02:
                continue
            column = x // cell
            blue = clamp(0.25 + 0.35 * x / width + 0.25 * (1 - y / height) + 0.45 * (wander[x, y] / 255 - 0.5))
            dark = floor + (fall - floor) * smooth(0.0, 0.85, math.hypot(x - lx, y - ly) / reach)
            if row_b[(column + 5) % 8] < dark:
                ink = GROUND
            else:
                ink = BLUE if row_a[column % 8] < blue else INK
            pixels[x, y] = (
                max(0, min(255, round(wg * g[0] + wp * p[0] + wk * ink[0]))),
                max(0, min(255, round(wg * g[1] + wp * p[1] + wk * ink[1]))),
                max(0, min(255, round(wg * g[2] + wp * p[2] + wk * ink[2]))),
                a,
            )
    return image


# ----------------------------------------------------------------------------
# Treatments. `level` maps sampled tone to the coverage of one color, 0 to 1.


def split(low: float, high: float, gamma: float = 1.0):
    """A level that rises from 0 at tone `low` to 1 at tone `high`."""
    def level(tone: float) -> float:
        return clamp((tone - low) / (high - low)) ** gamma
    return level


def halftone(plate: Plate, tone: Tone, pitch: float, angle: float, color, level, gain: float = 0.74,
             alpha: float = 1.0) -> None:
    """A screen of dots on a lattice turned by `angle` degrees. Dot area follows coverage."""
    c = plate.c
    cos, sin = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    cx, cy = plate.width / 2, plate.height / 2
    reach = int(math.hypot(plate.width, plate.height) / 2 / pitch) + 2
    c.new_path()
    for i in range(-reach, reach + 1):
        for j in range(-reach, reach + 1):
            x = cx + (i * cos - j * sin) * pitch
            y = cy + (i * sin + j * cos) * pitch
            if -pitch <= x <= plate.width + pitch and -pitch <= y <= plate.height + pitch:
                coverage = level(tone.at(x, y))
                if coverage > 0.006:
                    c.new_sub_path()
                    c.arc(x, y, pitch * gain * math.sqrt(coverage), 0, TAU)
    plate.color(color, alpha)
    c.fill()


def lines(plate: Plate, tone: Tone, pitch: float, angle: float, color, level, gain: float = 0.56,
          step: float = 2.0, wave: tuple[float, float] | None = None, alpha: float = 1.0) -> None:
    """Parallel lines turned by `angle` degrees whose width follows coverage: engraving, or scanlines at 0."""
    c = plate.c
    cos, sin = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    cx, cy = plate.width / 2, plate.height / 2
    reach = math.hypot(plate.width, plate.height) / 2 + pitch
    c.new_path()

    def flush(left: list, right: list) -> None:
        if len(left) > 1:
            c.move_to(*left[0])
            for point in left[1:]:
                c.line_to(*point)
            for point in reversed(right):
                c.line_to(*point)
            c.close_path()

    for k in range(-int(reach / pitch) - 1, int(reach / pitch) + 2):
        left: list = []
        right: list = []
        s = -reach
        while s <= reach:
            offset = k * pitch + (wave[0] * math.sin(s / wave[1] + k * 0.7) if wave else 0.0)
            x = cx + s * cos - offset * sin
            y = cy + s * sin + offset * cos
            inside = -2 <= x <= plate.width + 2 and -2 <= y <= plate.height + 2
            half = pitch * gain * level(tone.at(x, y)) if inside else 0.0
            if half > 0.16:
                left.append((x + half * sin, y - half * cos))
                right.append((x - half * sin, y + half * cos))
            else:
                flush(left, right)
                left, right = [], []
            s += step
        flush(left, right)
    plate.color(color, alpha)
    c.fill()


def stipple(plate: Plate, tone: Image.Image, cell: float, color, level, size: float = 0.47, jitter: float = 0.32,
            seed: int = 7) -> None:
    """Dots placed by Floyd-Steinberg error diffusion on a coarse grid, then nudged off the grid."""
    columns, rows = round(plate.width / cell), round(plate.height / cell)
    table = [round(255 * level(value / 255)) for value in range(256)]
    small = tone.resize((columns, rows), Image.LANCZOS).point(table).convert("1", dither=Image.Dither.FLOYDSTEINBERG)
    pixels = small.load()
    rng = random.Random(seed)
    c = plate.c
    c.new_path()
    for row in range(rows):
        for column in range(columns):
            if pixels[column, row]:
                x = (column + 0.5 + rng.uniform(-jitter, jitter)) * cell
                y = (row + 0.5 + rng.uniform(-jitter, jitter)) * cell
                c.new_sub_path()
                c.arc(x, y, cell * size * rng.uniform(0.82, 1.1), 0, TAU)
    plate.color(color)
    c.fill()


def bitmap(plate: Plate, tone: Image.Image, cell: int, color, level) -> None:
    """Square pixels placed by Atkinson dithering, the grain of an early bitmap display."""
    columns, rows = plate.width // cell, plate.height // cell
    table = [level(value / 255) for value in range(256)]
    values = [table[value] for value in tone.resize((columns, rows), Image.LANCZOS).getdata()]
    c = plate.c
    c.set_antialias(cairo.ANTIALIAS_NONE)
    c.new_path()
    for row in range(rows):
        base = row * columns
        for column in range(columns):
            index = base + column
            old = values[index]
            new = 1.0 if old >= 0.5 else 0.0
            error = (old - new) / 8
            if new:
                c.rectangle(column * cell, row * cell, cell, cell)
            # Atkinson spreads six eighths of the error and drops the rest.
            for dx, dy in ((1, 0), (2, 0), (-1, 1), (0, 1), (1, 1), (0, 2)):
                nx, ny = column + dx, row + dy
                if 0 <= nx < columns and ny < rows:
                    values[ny * columns + nx] += error
    plate.color(color)
    c.fill()
    c.set_antialias(cairo.ANTIALIAS_DEFAULT)


# ----------------------------------------------------------------------------
# Subject: the stained neuron and the circuit it becomes.


class Neuron:
    """A pyramidal neuron as tapering paths, grown from a seed."""

    def __init__(self, seed: int, x: float, y: float, scale: float) -> None:
        self.rng = random.Random(seed)
        self.x, self.y, self.scale = x, y, scale
        self.paths: list[list[tuple[float, float, float]]] = []
        self.tips: list[tuple[float, float, float]] = []
        up = -math.pi / 2
        # The apical dendrite climbs; basal dendrites spread from the base of the cell body.
        self.grow(x, y - 20 * scale, up, 330 * scale, 9.0 * scale, 4, pull=up)
        for angle in (2.45, 1.95, 1.2, 0.72, 3.0, 0.2):
            self.grow(x + 22 * scale * math.cos(angle), y + 16 * scale * math.sin(angle), angle,
                      self.rng.uniform(150, 215) * scale, 5.0 * scale, 3, pull=None)
        # The axon is thin and falls straight down.
        self.grow(x + 2 * scale, y + 26 * scale, math.pi / 2, 300 * scale, 2.2 * scale, 0, pull=math.pi / 2, taper=0.25)

    def grow(self, x: float, y: float, angle: float, length: float, width: float, depth: int, pull: float | None,
             taper: float = 0.72) -> None:
        rng = self.rng
        stride = 7.0 * self.scale
        steps = max(4, int(length / stride))
        path = [(x, y, width)]
        for index in range(steps):
            angle += rng.gauss(0, 0.085)
            if pull is not None:
                angle += (pull - angle) * 0.06
            x += math.cos(angle) * stride
            y += math.sin(angle) * stride
            current = width * (1 - taper * (index + 1) / steps)
            path.append((x, y, current))
            progress = (index + 1) / steps
            if depth > 0 and 0.12 < progress < 0.92 and rng.random() < 0.085:
                side = rng.choice((-1, 1))
                self.grow(x, y, angle + side * rng.uniform(0.5, 1.05), length * (1 - progress * 0.5) * rng.uniform(0.5, 0.78),
                          current * 0.7, depth - 1, pull=None)
        self.paths.append(path)
        if depth > 0:
            for side in (-1, 1):
                self.grow(x, y, angle + side * rng.uniform(0.28, 0.62), length * rng.uniform(0.42, 0.6),
                          path[-1][2] * 0.92, depth - 1, pull=None)
        else:
            self.tips.append((x, y, angle))

    def draw(self, c: cairo.Context, spines: bool = True) -> None:
        """Stroke the neuron with the context's current source."""
        scale, rng = self.scale, random.Random(11)
        for path in self.paths:
            for (x0, y0, w0), (x1, y1, w1) in zip(path, path[1:]):
                c.set_line_width(max(0.9 * scale, (w0 + w1) / 2))
                c.move_to(x0, y0)
                c.line_to(x1, y1)
                c.stroke()
                # Dendritic spines: short side twigs with a knob, on the fine branches only.
                if spines and w0 < 3.2 * scale and rng.random() < 0.3:
                    normal = math.atan2(y1 - y0, x1 - x0) + rng.choice((-1, 1)) * math.pi / 2
                    reach = rng.uniform(3.5, 6.5) * scale
                    sx, sy = x0 + math.cos(normal) * reach, y0 + math.sin(normal) * reach
                    c.set_line_width(0.8 * scale)
                    c.move_to(x0, y0)
                    c.line_to(sx, sy)
                    c.stroke()
                    c.arc(sx, sy, 1.15 * scale, 0, TAU)
                    c.fill()
        # The cell body: a rounded pyramid with its apex toward the apical dendrite.
        x, y, s = self.x, self.y, 30 * scale
        c.move_to(x, y - 1.25 * s)
        c.curve_to(x + 0.45 * s, y - 0.5 * s, x + 1.2 * s, y + 0.5 * s, x + 0.95 * s, y + 0.85 * s)
        c.curve_to(x + 0.5 * s, y + 1.2 * s, x - 0.5 * s, y + 1.2 * s, x - 0.95 * s, y + 0.85 * s)
        c.curve_to(x - 1.2 * s, y + 0.5 * s, x - 0.45 * s, y - 0.5 * s, x, y - 1.25 * s)
        c.fill()


def circuit(c: cairo.Context, tips: list[tuple[float, float, float]], left: float, right: float, top: float,
            bottom: float, scale: float, layers: tuple[int, ...] = (6, 4, 2)) -> None:
    """Carry dendrite tips into a layered circuit on the right of a plate, with the context's current source.

    Each chosen tip runs as a wire with square corners to an input of the first layer. The layers are drawn the
    way a feedforward circuit is diagrammed: nodes in columns, every node wired to the next column.
    """
    span = bottom - top
    columns = []
    for index, count in enumerate(layers):
        x = left + (right - left) * (index + 0.5) / len(layers)
        gap = span / (count + 1)
        columns.append([(x, top + gap * (row + 1)) for row in range(count)])

    c.set_line_width(1.5 * scale)
    c.set_line_cap(cairo.LINE_CAP_BUTT)
    for first, second in zip(columns, columns[1:]):
        for x0, y0 in first:
            for x1, y1 in second:
                c.move_to(x0, y0)
                c.line_to(x1, y1)
        c.stroke()

    inputs = columns[0]
    chosen = sorted(tips, key=lambda tip: tip[1])
    chosen = [chosen[round(index * (len(chosen) - 1) / (len(inputs) - 1))] for index in range(len(inputs))]
    for lane, ((tx, ty, _), (nx, ny)) in enumerate(zip(chosen, inputs)):
        # Each wire takes its own vertical lane so no two share a segment.
        bend = left - (lane + 1) * 13 * scale if ty < ny else left - (len(inputs) - lane) * 13 * scale
        bend = max(bend, tx + 10 * scale)
        c.set_line_width(1.7 * scale)
        c.move_to(tx, ty)
        c.line_to(bend, ty)
        c.line_to(bend, ny)
        c.line_to(nx, ny)
        c.stroke()
        for px, py in ((tx, ty), (bend, ty), (bend, ny)):
            c.rectangle(px - 3.2 * scale, py - 3.2 * scale, 6.4 * scale, 6.4 * scale)
        c.fill()

    for column in columns:
        for x, y in column:
            c.arc(x, y, 8.5 * scale, 0, TAU)
        c.fill()
    # The output leaves the plate.
    for x, y in columns[-1]:
        c.move_to(x, y)
        c.line_to(right + 400 * scale, y)
    c.stroke()
    c.set_line_cap(cairo.LINE_CAP_ROUND)


def slide_field(width: int, height: int, light: tuple[float, float], radius: float) -> Image.Image:
    """The tone of a lit slide: bright around `light`, falling off toward the edges."""
    canvas = Canvas(width, height, 0.36)
    canvas.radial(light[0], light[1], 0, radius, [(0, 1.0, 1), (0.28, 0.82, 1), (0.7, 0.56, 1), (1, 0.36, 1)])
    canvas.c.paint()
    return canvas.image()


def slide(name: str, width: int, height: int, cell: tuple[float, float], scale: float, seed: int,
          circuit_box: tuple[float, float, float, float] | None, light: tuple[float, float], pitch: float,
          layers: tuple[int, ...] = (6, 4, 2)) -> None:
    """A neuron in a lit field of ink, opaque black, complete, and alone; the circuit it becomes glows out of the dark."""
    field = Tone(slide_field(width, height, light, max(width, height) * 0.78), blur=pitch)
    plate = Plate(width, height, INK)
    # The ground darkens toward the edges in dots of black, and breaks into pale flecks under the light.
    halftone(plate, field, pitch, 28, GROUND, lambda tone: clamp((0.74 - tone) / 0.74) ** 1.5 * 0.62)
    halftone(plate, field, pitch, 28, PALE, split(0.9, 1.1, 1.5), gain=0.55)

    neuron = Neuron(seed, cell[0], cell[1], scale)
    plate.color(GROUND)
    neuron.draw(plate.c)
    if circuit_box:
        left, right, top, bottom = circuit_box
        tips = [tip for tip in neuron.tips if cell[0] + 40 * scale < tip[0] < left - 30 * scale and top < tip[1] < bottom]
        # The circuit the cell becomes is printed in the reading color, so it reads where the field sinks to dark.
        plate.color(PALE)
        circuit(plate.c, tips, left, right, top, bottom, scale, layers)
    plate.save(name, cell=max(3, round(width / 540)), light=light, fall=0.92, floor=0.3)


# ----------------------------------------------------------------------------
# Subject: the eye, from the project's own artwork.


def eye_tone(box: tuple[int, int, int, int], size: tuple[int, int], blur: float = 0.0) -> Image.Image:
    """A crop of the eye artwork as tone. The artwork is white line on black."""
    image = linearize(Image.open(EYE).convert("L")).crop(box).resize(size, Image.LANCZOS)
    return image.filter(ImageFilter.GaussianBlur(blur)) if blur else image


def graticule(c: cairo.Context, x: float, y: float, radius: float, scale: float) -> None:
    """A measuring reticle: ticks around a circle, with longer ones at the quarters."""
    for index in range(72):
        angle = index * TAU / 72
        inner = radius + (0 if index % 18 == 0 else 10 if index % 6 == 0 else 16) * scale
        outer = radius + 24 * scale
        c.move_to(x + math.cos(angle) * inner, y + math.sin(angle) * inner)
        c.line_to(x + math.cos(angle) * outer, y + math.sin(angle) * outer)
    c.set_line_width(1.6 * scale)
    c.stroke()
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        c.move_to(x + dx * (radius + 36 * scale), y + dy * (radius + 36 * scale))
        c.line_to(x + dx * (radius + 92 * scale), y + dy * (radius + 92 * scale))
    c.set_line_width(1.2 * scale)
    c.stroke()


def eye_scan(name: str = "eye-scan") -> None:
    """The eye in scanlines, with a reticle on the iris: an image read back line by line."""
    width, height, pitch = 720, 690, 7.0
    tone = eye_tone((262, 372, 962, 1043), (width, height), blur=1.6)
    sample = Tone(tone)
    plate = Plate(width, height, GROUND)
    lines(plate, sample, pitch, 0, INK, lambda value: 0.1 + 0.9 * clamp(value * 1.5) ** 0.7, gain=0.5)
    lines(plate, sample, pitch, 0, PALE, split(0.5, 1.0, 1.2), gain=0.34)
    plate.color(PALE, 0.9)
    graticule(plate.c, 300, 322, 128, 1.0)
    plate.save(name)


def eye_poster(name: str = "eye-poster") -> None:
    """The eye at the scale of a poster, in dots of ink that thin out into the dark of the closing panel."""
    width, height, pitch = 2160, 900, 9.0
    tone = eye_tone((120, 330, 1200, 780), (width, height), blur=3.0)
    # Fade the drawing out toward the bottom edge, where the panel's flat color takes over.
    fade = Canvas(width, height, 1.0)
    fade.linear(0, height * 0.42, 0, height * 0.97, [(0, 1.0, 0), (1, 0.0, 1)])
    fade.c.paint()
    tone = ImageChops.multiply(tone, fade.image())
    plate = Plate(width, height, GROUND)
    halftone(plate, Tone(tone), pitch, 22, INK, lambda value: clamp(value * 1.7) ** 0.8)
    halftone(plate, Tone(tone), pitch, 22, PALE, split(0.62, 1.0, 1.4), gain=0.42)
    plate.save(name, cell=4, light=(width * 0.42, height * 0.35), fall=0.55)


def eye_iris(name: str = "eye-iris") -> None:
    """The iris alone, in a coarse dot screen."""
    width, height, pitch = 1110, 600, 7.5
    tone = eye_tone((230, 430, 900, 792), (width, height), blur=2.0)
    sample = Tone(tone)
    plate = Plate(width, height, GROUND)
    halftone(plate, sample, pitch, 45, INK, lambda value: 0.02 + 0.98 * clamp(value * 1.5) ** 0.75)
    halftone(plate, sample, pitch, 45, PALE, split(0.5, 1.0, 1.1), gain=0.5)
    plate.save(name)


# ----------------------------------------------------------------------------
# Subject: a neuron as an engraving, printed on the page itself.


def neuron_engraving(name: str = "neuron-engraving") -> None:
    """A tall arbor printed on the page itself: a pale cell in an aura of engraved lines.

    The lower and outer edges crop it. Toward the top and the inner edge the lines thin out to nothing.
    """
    width, height = 760, 1500
    neuron = Neuron(23, 500, 1150, 2.3)
    canvas = Canvas(width, height, 0.0)
    canvas.gray(1.0)
    neuron.draw(canvas.c, spines=False)
    aura = canvas.image().filter(ImageFilter.GaussianBlur(30)).point(lambda value: min(255, int(value * 2.6)))
    fade = Canvas(width, height, 1.0)
    fade.linear(0, 0, 250, 0, [(0, 0.0, 1), (1, 0.0, 0)])
    fade.c.paint()
    fade.linear(0, 0, 0, 330, [(0, 0.0, 1), (1, 0.0, 0)])
    fade.c.paint()
    aura = ImageChops.multiply(aura, fade.image())
    plate = Plate(width, height, None)
    lines(plate, Tone(aura), 6.4, -62, INK, lambda value: clamp(value * 0.9) ** 1.25, gain=0.46, wave=(1.4, 52))
    # The cell is printed in the lightest color on a dark page and in the darkest on a light one.
    plate.color(PALE if THEME == "dark" else GROUND)
    neuron.draw(plate.c, spines=True)
    plate.save(name)


# ----------------------------------------------------------------------------
# Subject: a line of metal type, and the tree a parser makes of it.


def sorts_tone(width: int, height: int, rows: list[str], sort: tuple[int, int], turn: float):
    """Rows of type seen from above, drawn at twice the plate's size and turned.

    Returns the tone, a mask of the letters, the size of the unturned drawing, and the bottom of the stick.
    """
    big = Image.new("L", (width * 2, height * 2), 8)
    letters = Image.new("L", big.size, 0)
    draw = ImageDraw.Draw(big)
    sw, sh = sort
    font = ImageFont.truetype(str(FONTS / "RedHatMono-Regular.ttf"), int(sh * 0.66))
    columns = big.width // (sw + 4) + 3
    top = 34
    for r, line in enumerate(rows):
        y = top + r * (sh + 10)
        # Type is set mirrored and from right to left. Blank sorts fill the measure on both sides.
        offset = (columns - len(line)) // 2
        for column in range(columns):
            x = column * (sw + 4) - sw
            index = len(line) - 1 - (column - offset)
            letter = line[index] if 0 <= index < len(line) else " "
            draw.rectangle([x, y, x + sw, y + sh], fill=128 if letter != " " else 52)
            draw.line([x, y + 1, x + sw, y + 1], fill=225, width=4)
            draw.line([x + 1, y, x + 1, y + sh], fill=190, width=4)
            draw.line([x, y + sh - 2, x + sw, y + sh - 2], fill=20, width=5)
            draw.line([x + sw - 2, y, x + sw - 2, y + sh], fill=36, width=5)
            if letter != " ":
                glyph = Image.new("L", (sw, sh), 0)
                ImageDraw.Draw(glyph).text((sw / 2, sh / 2), letter, font=font, fill=255, anchor="mm")
                letters.paste(255, (x, y), ImageOps.mirror(glyph))
    # The composing stick: a bright rail under the last row, graduated like a rule.
    rail = top + len(rows) * (sh + 10) + 6
    draw.rectangle([0, rail, big.width, rail + 36], fill=165)
    draw.line([0, rail + 1, big.width, rail + 1], fill=250, width=4)
    for index, tick in enumerate(range(-40, big.width + 40, 16)):
        draw.line([tick, rail + 36, tick, rail + (8 if index % 5 == 0 else 20)], fill=18, width=3)

    def turned(image: Image.Image, fill: int) -> Image.Image:
        return image.rotate(turn, resample=Image.BICUBIC, fillcolor=fill).resize((width, height), Image.LANCZOS)

    return turned(big, 8), turned(letters, 0), big.size, rail + 36


def turn_point(x: float, y: float, size: tuple[int, int], turn: float) -> tuple[float, float]:
    """Where a point of the unturned drawing lands in the finished plate (half the drawing's size)."""
    cx, cy = size[0] / 2, size[1] / 2
    angle = -math.radians(turn)
    dx, dy = x - cx, y - cy
    return ((cx + dx * math.cos(angle) - dy * math.sin(angle)) / 2, (cy + dx * math.sin(angle) + dy * math.cos(angle)) / 2)


def type_tree(name: str = "type-tree") -> None:
    """Lines of type in a composing stick, with the syntax tree of the statement hanging below."""
    width, height, turn = 720, 480, 7.0
    rows = ["let console", "= import:Std", ".Io.Console;"]
    tone, letters, size, rail = sorts_tone(width, height, rows, (112, 158), turn)
    sample = Tone(tone, blur=1.2)
    plate = Plate(width, height, GROUND)
    halftone(plate, sample, 4.6, 18, INK, split(0.02, 0.5, 0.9))
    halftone(plate, sample, 4.6, 18, PALE, split(0.62, 1.0, 1.1), gain=0.58)
    plate.stamp(letters, PALE)

    # The tree hangs from the stick as a drafting overlay: five leaves, two branches, one root.
    def below(fraction: float, depth: float) -> tuple[float, float]:
        return turn_point(size[0] * fraction, rail + depth, size, turn)

    leaves = [0.26, 0.38, 0.52, 0.64, 0.76]
    left, right, root = below(0.32, 178), below(0.64, 178), below(0.48, 286)
    c = plate.c
    plate.color(PALE)
    c.set_line_width(1.6)
    for index, leaf in enumerate(leaves):
        c.move_to(*below(leaf, 4))
        c.line_to(*below(leaf, 84))
        c.line_to(*(left if index < 2 else right))
    for branch in (left, right):
        c.move_to(*branch)
        c.line_to(*root)
    c.stroke()
    for point in [below(leaf, 84) for leaf in leaves] + [left, right]:
        c.new_sub_path()
        c.arc(point[0], point[1], 5.5, 0, TAU)
    plate.color(GROUND)
    c.fill_preserve()
    plate.color(PALE)
    c.set_line_width(1.8)
    c.stroke()
    c.arc(root[0], root[1], 7.5, 0, TAU)
    c.fill()
    plate.save(name)


# ----------------------------------------------------------------------------
# Subject: a caliper closing on a single node.


def caliper(name: str = "caliper") -> None:
    """A vernier caliper measuring one node. The rule and its dimension lines lead across the plate."""
    width, height = 720, 480
    canvas = Canvas(width, height, 0.06)
    c = canvas.c
    canvas.radial(190, 120, 0, 620, [(0, 0.3, 1), (1, 0.03, 1)])
    c.paint()
    c.save()
    c.translate(330, 150)
    c.rotate(math.radians(-14))
    # The beam, lit from above.
    canvas.linear(0, -34, 0, 34, [(0, 0.95, 1), (0.25, 0.74, 1), (1, 0.42, 1)])
    c.rectangle(-560, -34, 1200, 68)
    c.fill()
    # The fixed jaw, at the left.
    canvas.linear(-250, 0, -160, 0, [(0, 0.9, 1), (1, 0.5, 1)])
    c.move_to(-250, -34); c.line_to(-164, -34); c.line_to(-164, 240); c.line_to(-196, 262); c.line_to(-250, 100)
    c.close_path(); c.fill()
    # The slider and its jaw.
    canvas.linear(0, -48, 0, 70, [(0, 1.0, 1), (0.4, 0.8, 1), (1, 0.5, 1)])
    c.rectangle(-64, -48, 218, 118)
    c.fill()
    canvas.gray(0.07)
    c.rectangle(-44, -30, 150, 44)
    c.fill()
    canvas.linear(0, -30, 0, 14, [(0, 0.86, 1), (1, 0.62, 1)])
    c.rectangle(-40, -26, 142, 36)
    c.fill()
    canvas.linear(-64, 0, 30, 0, [(0, 0.5, 1), (1, 0.92, 1)])
    c.move_to(-64, 60); c.line_to(22, 60); c.line_to(-32, 100); c.line_to(-32, 262); c.line_to(-64, 240)
    c.close_path(); c.fill()
    # The thumb wheel, knurled.
    canvas.radial(170, 78, 2, 30, [(0, 1.0, 1), (1, 0.5, 1)])
    c.arc(170, 78, 26, 0, TAU); c.fill()
    canvas.gray(0.1)
    for index in range(18):
        angle = index * TAU / 18
        c.move_to(170 + math.cos(angle) * 18, 78 + math.sin(angle) * 18)
        c.line_to(170 + math.cos(angle) * 26, 78 + math.sin(angle) * 26)
    c.set_line_width(2); c.stroke()
    # The node held between the jaws.
    canvas.radial(-128, 138, 4, 62, [(0, 1.0, 1), (0.5, 0.86, 1), (1, 0.34, 1)])
    c.arc(-114, 152, 50, 0, TAU); c.fill()
    c.restore()
    tone = canvas.image()

    sample = Tone(tone, blur=1.0)
    plate = Plate(width, height, GROUND)
    lines(plate, sample, 5.6, 38, INK, split(0.02, 0.6, 0.9), gain=0.54)
    lines(plate, sample, 5.6, 38, PALE, split(0.62, 1.0, 1.2), gain=0.34)

    c = plate.c
    c.save()
    c.translate(330, 150)
    c.rotate(math.radians(-14))
    # The graduations are cut into the metal: drawn crisp, over the engraved lines.
    plate.color(GROUND)
    for index, x in enumerate(range(-550, 640, 8)):
        c.rectangle(x, -34, 2.2, 30 if index % 10 == 0 else 21 if index % 5 == 0 else 13)
    for index, x in enumerate(range(-34, 96, 7)):
        long = 21 if index % 5 == 0 else 12
        c.rectangle(x, 10 - long, 1.9, long)
    c.fill()
    c.select_font_face("monospace", cairo.FONT_SLANT_NORMAL, cairo.FONT_WEIGHT_BOLD)
    c.set_font_size(17)
    for index, x in enumerate(range(-550, 640, 80)):
        if not -70 < x < 160:
            c.move_to(x + 7, 16)
            c.show_text(str(index % 10))
    # Dimension lines for the opening of the jaws, as a draughtsman would add them.
    plate.color(PALE)
    c.set_line_width(1.5)
    for x in (-164, -64):
        c.move_to(x, 268); c.line_to(x, 304)
    c.move_to(-164, 292); c.line_to(-64, 292)
    c.stroke()
    for x, direction in ((-164, 1), (-64, -1)):
        c.move_to(x, 292); c.line_to(x + direction * 13, 287); c.line_to(x + direction * 13, 297); c.close_path()
    c.fill()
    c.restore()
    plate.save(name)


# ----------------------------------------------------------------------------
# Subject: tape reels, for a run that can be recorded and played again.


def reel(canvas: Canvas, x: float, y: float, radius: float, pack: float, turn: float) -> None:
    c = canvas.c
    # The flange, with two soft reflections.
    canvas.radial(x - radius * 0.3, y - radius * 0.35, radius * 0.1, radius * 1.25, [(0, 0.78, 1), (1, 0.4, 1)])
    c.arc(x, y, radius, 0, TAU); c.fill()
    for start in (turn + 0.5, turn + 0.5 + math.pi):
        canvas.gray(1.0, 0.3)
        c.move_to(x, y); c.arc(x, y, radius, start, start + 0.38); c.close_path(); c.fill()
    # Three windows show the wound tape, or the dark behind the reel where the pack ends.
    for index in range(3):
        start = turn + index * TAU / 3 + 0.2
        end = start + TAU / 3 - 0.4
        canvas.gray(0.05)
        c.arc(x, y, radius * 0.86, start, end); c.arc_negative(x, y, radius * 0.4, end, start); c.close_path(); c.fill()
        canvas.gray(0.3)
        c.arc(x, y, pack, start, end); c.arc_negative(x, y, radius * 0.4, end, start); c.close_path(); c.fill()
        canvas.gray(0.5)
        c.set_line_width(1.2)
        ring = radius * 0.4 + 5
        while ring < pack:
            c.arc(x, y, ring, start, end); c.stroke()
            ring += 5
    canvas.gray(1.0)
    c.set_line_width(3); c.arc(x, y, radius - 2, 0, TAU); c.stroke()
    # The hub.
    canvas.radial(x - 12, y - 14, 4, radius * 0.36, [(0, 1.0, 1), (1, 0.62, 1)])
    c.arc(x, y, radius * 0.33, 0, TAU); c.fill()
    canvas.gray(0.0)
    c.arc(x, y, radius * 0.115, 0, TAU); c.fill()
    canvas.gray(0.42)
    c.set_line_width(radius * 0.022)
    c.arc(x, y, radius * 0.24, 0, TAU); c.stroke()


def tape(name: str = "tape") -> None:
    """Two reels and the tape between them, in the square grain of an early bitmap."""
    width, height = 720, 690
    canvas = Canvas(width, height, 0.04)
    c = canvas.c
    canvas.radial(250, 250, 0, 640, [(0, 0.2, 1), (1, 0.02, 1)])
    c.paint()
    # The tape runs from the upper reel, under a guide, to the lower reel.
    path = [(246, 470), (470, 600), (600, 548)]
    canvas.gray(0.95)
    c.set_line_width(4)
    c.move_to(*path[0]); c.line_to(*path[1]); c.line_to(*path[2]); c.stroke()
    reel(canvas, 235, 228, 262, 172, 0.35)
    reel(canvas, 742, 700, 250, 120, 1.4)
    canvas.radial(466, 596, 2, 24, [(0, 1.0, 1), (1, 0.55, 1)])
    c.arc(470, 600, 20, 0, TAU); c.fill()
    tone = canvas.image()

    plate = Plate(width, height, GROUND)
    bitmap(plate, tone, 4, INK, split(0.0, 0.6, 0.85))
    bitmap(plate, tone, 4, PALE, split(0.62, 1.04, 1.0))
    # Records along the tape: one tick for each, at an even spacing.
    c = plate.c
    plate.color(PALE)
    (x0, y0), (x1, y1) = path[0], path[1]
    length = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / length, (y1 - y0) / length
    for index in range(3, int(length / 12) - 1):
        px, py = x0 + ux * index * 12, y0 + uy * index * 12
        reach = 16 if index % 5 == 0 else 9
        c.move_to(px - uy * 7, py + ux * 7); c.line_to(px - uy * (7 + reach), py + ux * (7 + reach))
    c.set_line_width(1.6); c.stroke()
    plate.save(name)


# ----------------------------------------------------------------------------
# Subject: a balance, level.


def balance(name: str = "balance") -> None:
    """An equal-arm balance at rest: a circuit in one pan, the weights it must equal in the other."""
    width, height = 720, 690
    canvas = Canvas(width, height, 0.03)
    c = canvas.c
    canvas.radial(360, 300, 0, 470, [(0, 0.17, 1), (1, 0.0, 1)])
    c.paint()
    c.translate(360, 318)
    c.scale(1.17, 1.17)
    c.translate(-360, -318)
    # The pillar and its stepped base, which the bottom edge crops.
    canvas.linear(344, 0, 378, 0, [(0, 0.95, 1), (0.5, 0.7, 1), (1, 0.34, 1)])
    c.rectangle(349, 150, 22, 500); c.fill()
    for inset, top in ((70, 632), (120, 660)):
        canvas.linear(0, top, 0, top + 40, [(0, 0.95, 1), (1, 0.45, 1)])
        c.rectangle(360 - inset, top, inset * 2, 60); c.fill()
    # The beam on its knife edge.
    canvas.linear(0, 138, 0, 156, [(0, 1.0, 1), (1, 0.55, 1)])
    c.move_to(96, 150); c.line_to(360, 132); c.line_to(624, 150); c.line_to(624, 158); c.line_to(96, 158)
    c.close_path(); c.fill()
    canvas.gray(1.0)
    c.move_to(360, 118); c.line_to(374, 150); c.line_to(346, 150); c.close_path(); c.fill()
    # The pointer hangs straight down over a scale: the beam is level.
    c.set_line_width(2.4)
    c.move_to(360, 158); c.line_to(360, 300); c.stroke()
    c.set_line_width(3.2)
    for index in range(-3, 4):
        angle = math.pi / 2 + index * 0.085
        inner = 142 if index else 128
        c.move_to(360 + math.cos(angle) * inner, 150 + math.sin(angle) * inner)
        c.line_to(360 + math.cos(angle) * 166, 150 + math.sin(angle) * 166)
    c.stroke()
    for px in (112, 608):
        canvas.gray(1.0)
        c.set_line_width(2.8)
        for dx in (-88, 0, 88):
            c.move_to(px, 158); c.line_to(px + dx, 452)
        c.stroke()
        # The pan: a shallow dish, lit on its rim.
        canvas.linear(0, 452, 0, 500, [(0, 0.5, 1), (1, 0.2, 1)])
        c.save(); c.translate(px, 452); c.scale(1, 0.42); c.arc(0, 0, 100, 0, math.pi); c.restore()
        c.close_path(); c.fill()
        canvas.gray(0.98)
        c.save(); c.translate(px, 452); c.scale(1, 0.14); c.arc(0, 0, 100, 0, TAU); c.restore(); c.fill()
        canvas.gray(0.2)
        c.save(); c.translate(px, 453); c.scale(1, 0.1); c.arc(0, 0, 90, 0, TAU); c.restore(); c.fill()
    # Left pan: three turned weights.
    for x, w, h in ((78, 34, 52), (118, 28, 40), (150, 22, 30)):
        canvas.linear(x - w / 2, 0, x + w / 2, 0, [(0, 1.0, 1), (0.55, 0.78, 1), (1, 0.4, 1)])
        c.rectangle(x - w / 2, 450 - h, w, h); c.fill()
        c.rectangle(x - w * 0.2, 450 - h - 9, w * 0.4, 9); c.fill()
        c.arc(x, 450 - h - 12, w * 0.24, 0, TAU); c.fill()
    # Right pan: a small circuit, two inputs into one node.
    canvas.gray(0.96)
    c.set_line_width(3)
    for x in (574, 642):
        c.move_to(x, 428); c.line_to(608, 384)
    c.stroke()
    for x, y, r in ((574, 428, 13), (642, 428, 13), (608, 384, 17)):
        canvas.radial(x - 4, y - 5, 1, r, [(0, 1.0, 1), (1, 0.7, 1)])
        c.arc(x, y, r, 0, TAU); c.fill()
    tone = canvas.image()

    plate = Plate(width, height, GROUND)
    stipple(plate, tone, 3.2, INK, split(0.03, 0.5, 0.8))
    stipple(plate, tone, 3.2, PALE, split(0.56, 0.98, 1.0), size=0.44, seed=19)
    plate.save(name)


# ----------------------------------------------------------------------------
# Subject: a card catalogue, with one drawer open.


def catalogue(name: str = "catalogue") -> None:
    """Drawers of a card catalogue in engraved lines. One is drawn out."""
    width, height = 1110, 600
    canvas = Canvas(width, height, 0.1)
    c = canvas.c
    dw, dh, gap = 218, 190, 12
    for row in range(-1, 4):
        for column in range(-1, 6):
            x = column * (dw + gap) + 28
            y = row * (dh + gap) + 40
            opened = (row, column) == (1, 3)
            canvas.linear(x, y, x, y + dh, [(0, 0.5, 1), (1, 0.3, 1)]) if not opened else canvas.linear(
                x, y, x, y + dh, [(0, 0.86, 1), (1, 0.6, 1)])
            c.rectangle(x, y, dw, dh); c.fill()
            canvas.gray(0.7 if not opened else 1.0)
            c.rectangle(x, y, dw, 3); c.fill()
            canvas.gray(0.04)
            c.rectangle(x, y + dh - 4, dw, 4); c.rectangle(x + dw - 4, y, 4, dh); c.fill()
            # The label holder and its typed card.
            canvas.gray(0.14)
            c.rectangle(x + 44, y + 36, 130, 54); c.fill()
            canvas.gray(0.94 if opened else 0.78)
            c.rectangle(x + 50, y + 42, 118, 42); c.fill()
            canvas.gray(0.12)
            for line, length in enumerate((84, 60, 72)):
                c.rectangle(x + 58, y + 50 + line * 10, length, 3.5)
            c.fill()
            # The pull.
            canvas.gray(0.96)
            c.set_line_width(7)
            c.arc(x + dw / 2, y + 132, 20, 0.15, math.pi - 0.15); c.stroke()
            canvas.gray(0.1)
            c.set_line_width(2)
            c.arc(x + dw / 2, y + 137, 20, 0.15, math.pi - 0.15); c.stroke()
            if opened:
                # Cards stand in the open drawer, above its front.
                for card in range(13):
                    canvas.gray(0.98 if card % 3 else 0.72)
                    c.rectangle(x + 10, y - 12 - card * 5.5, dw - 20, 3.4)
                c.fill()
                canvas.gray(0.02)
                c.rectangle(x - 6, y + dh, dw + 12, 16); c.fill()
    tone = canvas.image()
    sample = Tone(tone, blur=1.1)
    plate = Plate(width, height, GROUND)
    lines(plate, sample, 6.0, -32, INK, split(0.02, 0.62, 0.9), gain=0.54)
    lines(plate, sample, 6.0, -32, PALE, split(0.66, 1.0, 1.2), gain=0.34)
    plate.save(name)


# ----------------------------------------------------------------------------
# Subject: a core memory plane.


def cores(name: str = "cores") -> None:
    """Ferrite cores threaded on a lattice of wires: one bit of state at every crossing."""
    width, height = 1110, 600
    canvas = Canvas(width, height, 0.03)
    c = canvas.c
    canvas.radial(380, 240, 0, 900, [(0, 0.2, 1), (1, 0.02, 1)])
    c.paint()
    pitch = 62
    c.save()
    c.translate(width / 2, height / 2)
    c.rotate(math.radians(45))
    reach = 15
    canvas.gray(0.5)
    c.set_line_width(2.4)
    for index in range(-reach, reach + 1):
        c.move_to(index * pitch, -reach * pitch); c.line_to(index * pitch, reach * pitch)
        c.move_to(-reach * pitch, index * pitch); c.line_to(reach * pitch, index * pitch)
    c.stroke()
    # The sense wire runs diagonally through every core.
    canvas.gray(0.3)
    c.set_line_width(1.4)
    for index in range(-2 * reach, 2 * reach + 1, 2):
        c.move_to(index * pitch - reach * pitch, -reach * pitch); c.line_to(index * pitch + reach * pitch, reach * pitch)
    c.stroke()
    for i in range(-reach, reach + 1):
        for j in range(-reach, reach + 1):
            c.save()
            c.translate(i * pitch, j * pitch)
            c.rotate(math.radians(45 if (i + j) % 2 else -45))
            c.scale(1, 0.46)
            # A lit core here and there: a stored one.
            lit = (i * 7 + j * 13) % 11 == 0
            canvas.gray(1.0 if lit else 0.8)
            c.arc(0, 0, 21, 0, TAU); c.fill()
            canvas.gray(0.03)
            c.arc(0, 0, 10.5, 0, TAU); c.fill()
            c.restore()
            if lit:
                canvas.gray(1.0)
                c.arc(i * pitch, j * pitch, 4.2, 0, TAU); c.fill()
    c.restore()
    tone = canvas.image()
    plate = Plate(width, height, GROUND)
    bitmap(plate, tone, 3, INK, split(0.0, 0.58, 0.9))
    bitmap(plate, tone, 3, PALE, split(0.84, 1.02))
    plate.save(name)


# ----------------------------------------------------------------------------
# The logo, the tab icon, and the paper grain. They are not plates, but they are made here so that every image of
# the site comes from this script.


LOGO = Path("artwork/logo.png")


def logo() -> None:
    """The project's logo, artwork/logo.png, reversed for a dark page, and the tab icon made from it.

    The logo is slate lines on a transparent ground, with white light in the iris, drawn for a light page. On the
    site's dark ground the slate nearly vanishes, so the site shows the same drawing reversed: each pixel keeps its
    shape, and its darkness becomes its opacity. `logo-mask.png` is that opacity, in white; the stylesheet paints
    it in a color of the palette with `mask-image`, so the logo follows the palette without being drawn again.
    """
    assets = ASSETS
    source = Image.open(LOGO).convert("RGBA")
    width, height = source.size
    # The darkest tone of the drawing is the slate of its lines; white is its lightest.
    line = 128.0
    mask = Image.new("LA", (width, height))
    pixels, out = source.load(), mask.load()
    for y in range(height):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            lightness = 0.2126 * r + 0.7152 * g + 0.0722 * b
            coverage = min(1.0, max(0.0, (255.0 - lightness) / (255.0 - line)))
            out[x, y] = (255, round(a * coverage))
    # Three times the largest size a page shows it at, for screens with dense pixels.
    mask.resize((480, 384), Image.LANCZOS).save(assets / "logo-mask.png", optimize=True)
    # The tab icon: the reversed logo in the reading color on a square of the ground color.
    icon = Image.new("RGBA", (192, 192), GROUND + (255,))
    mark = Image.new("RGBA", (184, 147), PALE + (0,))
    mark.putalpha(mask.resize((184, 147), Image.LANCZOS).getchannel("A"))
    icon.alpha_composite(mark, (4, 22))
    icon.convert("RGB").resize((96, 96), Image.LANCZOS).save(assets / "icon.png", optimize=True)
    print("site/assets/logo-mask.png, icon.png")


def band() -> None:
    """A strip of ordered dither: pink into blue along its length, thinning to nothing toward its top edge.

    The stylesheet repeats it along the edges the front page draws between its parts, so that the page's own
    rules are made of the same counted marks as its plates.
    """
    width, height, cell = 1440, 18, 3
    image = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    pixels = image.load()
    for y in range(height):
        density = smooth(0.0, 1.0, (y + 0.5) / height) ** 1.6
        for x in range(width):
            row = BAYER[(y // cell) % 8]
            threshold = row[(x // cell) % 8]
            if threshold >= density:
                continue
            blue = 0.5 + 0.5 * math.sin(x / width * TAU)
            other = BAYER[(y // cell + 3) % 8][(x // cell + 5) % 8]
            pixels[x, y] = (BLUE if other < blue else INK) + (255,)
    name = "band.png" if THEME == "dark" else f"band-{THEME}.png"
    image.save(ASSETS / name, optimize=True)
    print(f"site/assets/{name}  {(ASSETS / name).stat().st_size // 1024} KB")


def grit() -> None:
    """A tile of the light theme's outer field: the dark blue, broken by an ordered dither of brighter blue."""
    size, cell = 64, 2
    rng = random.Random(11)
    image = Image.new("RGB", (size, size), GROUND)
    pixels = image.load()
    for y in range(size):
        for x in range(size):
            threshold = BAYER[(y // cell) % 8][(x // cell) % 8]
            value = 0.18 + 0.22 * rng.random()
            if threshold < value * 0.55:
                pixels[x, y] = BLUE
            elif threshold > 1 - value * 0.12:
                pixels[x, y] = tuple(round(g * 0.72) for g in GROUND)
    name = "grit.png" if THEME == "dark" else f"grit-{THEME}.png"
    image.save(ASSETS / name, optimize=True)
    print(f"site/assets/{name}")


def grain() -> None:
    """A tile of faint paper grain: sparse pale and dark specks on nothing."""
    size = 240
    rng = random.Random(3)
    image = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    pixels = image.load()
    for _ in range(size * size // 7):
        x, y = rng.randrange(size), rng.randrange(size)
        if rng.random() < 0.62:
            pixels[x, y] = PALE + (rng.randrange(5, 15),)
        else:
            pixels[x, y] = (0, 0, 0, rng.randrange(14, 40))
    image.save(ASSETS / "grain.png", optimize=True)
    print(f"site/assets/grain.png  {(ASSETS / 'grain.png').stat().st_size // 1024} KB")


# ----------------------------------------------------------------------------


PLATES = {
    "neuron-slide": lambda: slide("neuron-slide", 720, 480, (250, 300), 0.74, 5, (505, 690, 60, 420), (300, 210), 6.0),
    "neuron-hero": lambda: slide("neuron-hero", 2160, 780, (640, 470), 1.5, 5, (1330, 2040, 110, 670), (760, 330), 9.0,
                                 layers=(8, 6, 4, 2)),
    "neuron-engraving": neuron_engraving,
    "eye-scan": eye_scan,
    "eye-iris": eye_iris,
    "eye-poster": eye_poster,
    "type-tree": type_tree,
    "caliper": caliper,
    "tape": tape,
    "balance": balance,
    "catalogue": catalogue,
    "cores": cores,
    "logo": logo,
    "band": band,
    "grit": grit,
    "grain": grain,
}
# What the light theme needs: every plate, and its own band and grit. The logo, its icon, and the grain serve both.
LIGHT = [name for name in PLATES if name not in ("logo", "grain")]


def use_theme(theme: str) -> None:
    """Draw in `theme`'s colors from now on, into its directory."""
    global THEME, GROUND, INK, BLUE, PALE, OUT
    THEME = theme
    GROUND, INK, BLUE, PALE = (THEMES[theme][key] for key in ("GROUND", "INK", "BLUE", "PALE"))
    OUT = PLATES_DIR if theme == "dark" else PLATES_DIR / theme


def main() -> int:
    arguments = sys.argv[1:]
    themes = ["dark", "light"]
    if arguments[:1] in (["--dark"], ["--light"]):
        themes = [arguments.pop(0)[2:]]
    names = arguments or list(PLATES)
    unknown = [name for name in names if name not in PLATES]
    if unknown:
        print(f"unknown plate: {', '.join(unknown)}; the plates are {', '.join(PLATES)}")
        return 1
    for theme in themes:
        use_theme(theme)
        for name in names:
            if theme == "dark" or name in LIGHT:
                PLATES[name]()
    return 0


if __name__ == "__main__":
    sys.exit(main())
