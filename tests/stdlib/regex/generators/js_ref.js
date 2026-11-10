// Reads [[pattern, flags, input], ...] (JSON, Neura pattern syntax limited
// to what the generator emits) and prints, per case, the find_all spans
// (UTF-8 byte offsets, groups included) and whether the whole input matches,
// with ECMAScript semantics.
const fs = require('fs');
const cases = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
const enc = new TextEncoder();
const bytes = (s, i) => enc.encode(s.slice(0, i)).length;
const out = [];
for (const [pattern, flags, input] of cases) {
  const source = pattern.replace(/\(\?P</g, '(?<');
  let f = 'dg';
  if (flags & 1) f += 'i';
  if (flags & 2) f += 'm';
  if (flags & 4) f += 's';
  const re = new RegExp(source, f);
  const values = [];
  let pos = 0;
  while (pos <= input.length) {
    re.lastIndex = pos;
    const m = re.exec(input);
    if (!m) break;
    for (const span of m.indices) {
      if (span === undefined) { values.push(-1, -1); }
      else { values.push(bytes(input, span[0]), bytes(input, span[1])); }
    }
    const start = m.index, end = m.index + m[0].length;
    pos = end === start ? end + 1 : end;
  }
  const whole = new RegExp('(?<![\\s\\S])(?:' + source + ')(?![\\s\\S])', f.replace('g', '').replace('d', ''));
  out.push([values, whole.test(input)]);
}
console.log(JSON.stringify(out));
