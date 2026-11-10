# Prints the UTF-8 vectors of tests/stdlib/strings/utf8-tests.nra: each case
# is a byte list, its replacement decoding as scalar values (Python's
# bytes.decode('utf-8', 'replace'), Unicode's maximal subpart practice), and
# the expected from_bytes error offset and message (UnicodeDecodeError.start
# and .reason), then the lossy totals of the exhaustive tests.
#     python3 -I -B tests/stdlib/strings/generators/utf8-vectors.py
cases = [
    b"", b"A", b"\x00", b"\x7f", b"\x80", b"\xbf", b"\xc0", b"\xc1", b"\xc0\x80", b"\xc1\xbf",
    b"\xc2", b"\xc2\x80", b"\xc2\x7f", b"\xdf\xbf", b"\xdf\xc0", b"\xe0", b"\xe0\x80", b"\xe0\x9f\xbf",
    b"\xe0\xa0", b"\xe0\xa0\x80", b"\xe0\xa0\x7f", b"\xe1\x80\x80", b"\xec\xbf\xbf", b"\xed\x80\x80",
    b"\xed\x9f\xbf", b"\xed\xa0\x80", b"\xed\xbf\xbf", b"\xee\x80\x80", b"\xef\xbf\xbf", b"\xef\xbf",
    b"\xf0", b"\xf0\x80\x80\x80", b"\xf0\x8f\xbf\xbf", b"\xf0\x90", b"\xf0\x90\x80", b"\xf0\x90\x80\x80",
    b"\xf0\x90\x80\x7f", b"\xf1\x80\x80\x80", b"\xf3\xbf\xbf\xbf", b"\xf4\x8f\xbf\xbf", b"\xf4\x90\x80\x80",
    b"\xf5\x80\x80\x80", b"\xf8\x88\x80\x80\x80", b"\xfe", b"\xff", b"\xfe\xff",
    b"a\xffb", b"a\xe2\x82b", b"\xe2\x82\xac\xe2\x82", b"\xe2\x82\xac\xff\xe2\x82\xac",
    b"\xf0\x9f\x99\x82\xf0\x9f\x99", b"\xf0\x9f\x99A", b"\xc3\xa9\xc3", b"\xc3\xa9\xc3\xc3\xa9",
    b"\x61\xf1\x80\x80\xe1\x80\xc2\x62\x80\x63\x80\xbf\x64",
    b"\xe0\x80\xaf", b"\xf4\x91\x92\x93\xff\x41\x80\xbf\x42", b"\xe1\x80\xe2\xf0\x91\x92\xf1\xbf\x41",
    b"\xed\xa0\x80\xed\xbf\xbf", b"\x80\x80\x80", b"\xc2\xc2\x80",
]
def scalars(s):
    return [ord(c) for c in s]
def first_error(b):
    try:
        b.decode('utf-8')
        return None
    except UnicodeDecodeError as e:
        reason = e.reason
        return (e.start, reason)
out = []
for b in cases:
    lossy = b.decode('utf-8', 'replace')
    err = first_error(b)
    if err is None:
        expect = "-1, \"\""
    else:
        expect = "%d, \"%s\"" % err
    out.append("        check_bytes([%s], [%s], %s);" % (", ".join(str(x) for x in b), ", ".join(str(x) for x in scalars(lossy)), expect))
print("\n".join(out))

def totals(sequences):
    count = 0
    total = 0
    for candidate in sequences:
        s = (bytes(candidate) + b"\xff").decode('utf-8', 'replace')
        count += len(s)
        total += sum(ord(c) for c in s)
    return count, total

print("// two-byte totals: %d %d" % totals([a, b] for a in range(128, 256) for b in range(256)))
print("// long totals: %d %d" % totals([a, b, c, 0x80] for a in range(224, 248) for b in range(256)
                                       for c in (0x41, 0x80, 0xbf)))
