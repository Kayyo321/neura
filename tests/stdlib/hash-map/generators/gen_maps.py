# Generates stdlib/std/collections/int-map.nra and string-map.nra from
# hash-map.nra: the same class with the key type fixed and the key operations
# called directly. Rerun after any change to hash-map.nra:
#     python3 -I tests/stdlib/hash-map/generators/gen_maps.py
import pathlib
import re
import sys

collections = pathlib.Path(__file__).resolve().parents[4] / "stdlib" / "std" / "collections"
src = (collections / "hash-map.nra").read_text()
# Split off the factories: everything from "    // Keys compared with equals" up to "    let spread" is replaced.
start = src.index('    // Keys compared with equals and hashed with hash_code.')
end = src.index('    let spread = func')
body = src[:start] + '@@FACTORIES@@' + src[end:]
# The universal helpers are not needed.
def variant(name, key_type, header, factories):
    s = body
    s = s.replace('        let hash_of: func(K) -> uint32;\n        let same: func(K, K) -> bool;\n', '')
    s = s.replace('export let init = func(seal: Seal, table: hash_core.HashCore, hash: func(K) -> uint32, equal: func(K, K) -> bool) {\n            core = table;\n            hash_of = hash;\n            same = equal;\n        };',
                  'export let init = func(seal: Seal, table: hash_core.HashCore) {\n            core = table;\n        };')
    s = s.replace('HashMapIterator<K, V>', name + 'Iterator<V>')
    s = s.replace('HashMapIterator', name + 'Iterator')
    s = s.replace('HashMap<K, V>', name + '<V>')
    s = s.replace('class<K, V>', 'class<V>')
    s = re.sub(r'\bK\b', key_type, s)
    s = s.replace('spread(hash_of(key))', 'spread(key.hash_code())')
    s = s.replace('hash_of(stored_keys[entry])', 'stored_keys[entry].hash_code()')
    s = s.replace('same(key, stored_keys[entry])', 'key == stored_keys[entry]')
    s = s.replace('export let HashMap = module (', 'export let ' + name + ' = module (')
    # header comment
    s = s[s.index('export let ' + name + ' = module ('):]
    imports = src[:src.index('\n\n')] + '\n\n'
    s = s.replace('HashMap', name)
    s = imports + header + s
    s = s.replace('@@FACTORIES@@', factories)
    assert 'K,' not in s and '<K>' not in s and 'hash_of' not in s and 'same(' not in s, [l for l in s.splitlines() if 'K,' in l or '<K>' in l or 'hash_of' in l or 'same(' in l]
    return s

int_header = '''// An insertion-ordered hash map with int keys (Stdlib.md section 9.6). It
// follows the map contract of HashMap with the key type fixed and calls the
// key operations directly: the hash is Int32.hash (the value's two's
// complement pattern) and keys are compared with ==.
//
// Setting the value of an existing key keeps its position and is not a
// structural change. A key removed and added again goes to the end. A
// removed key and value stay reachable until the next compaction, never
// more than max(size, 7) of them. Iterators and callbacks fail fast with
// "collection modified during iteration".
'''
int_factories = '''    export let create = func<V> -> IntMap<V> => new IntMap<V>(Seal.Token, hash_core.create());

    // A table sized for `capacity` entries. Traps with "invalid argument:
    // capacity" for a negative capacity or one above 2^29.
    export let with_capacity = func<V>(capacity: int) -> IntMap<V> =>
        new IntMap<V>(Seal.Token, hash_core.with_capacity(capacity));

'''
string_header = '''// An insertion-ordered hash map with string keys (Stdlib.md section 9.6). It
// follows the map contract of HashMap with the key type fixed and calls the
// key operations directly: the hash is Strings.hash (31 times the hash plus
// each byte) and keys are compared with == (the same bytes).
//
// Setting the value of an existing key keeps its position and is not a
// structural change. A key removed and added again goes to the end. A
// removed key and value stay reachable until the next compaction, never
// more than max(size, 7) of them. Iterators and callbacks fail fast with
// "collection modified during iteration".
'''
string_factories = int_factories.replace('IntMap', 'StringMap')
(collections / 'int-map.nra').write_text(variant('IntMap', 'int', int_header, int_factories))
(collections / 'string-map.nra').write_text(variant('StringMap', 'string', string_header, string_factories))
