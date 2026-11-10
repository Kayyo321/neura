# Generates stdlib/std/collections/int-map.nra and string-map.nra from
# hash-map.nra: the same class with the key type fixed and the key operations
# called directly. Rerun after any change to hash-map.nra:
#     python3 -I tests/stdlib/hash-map/generators/gen_maps.py
import pathlib
import re
import sys

collections = pathlib.Path(__file__).resolve().parents[4] / "stdlib" / "std" / "collections"
src = (collections / "hash-map.nra").read_text()


# Replace the factories together with their attached documentation.
def documented_start(signature):
    return re.search(r'(?m)(?:^    ///[^\n]*\n)*(?=^    ' + re.escape(signature) + ')', src).start()


start = documented_start('export let create = func')
end = documented_start('let spread = func')
body = src[:start] + '@@FACTORIES@@' + src[end:]
# The universal helpers are not needed.
def variant(name, key_type, header, factories):
    s = body
    s = re.sub(r'(?m)(?:^        ///[^\n]*\n)*^        let (?:hash_of|same):[^\n]*\n', '', s)
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

int_header = '''/// @brief Insertion-ordered hash maps with int keys.
/// Keys are compared by numeric value and hashed by their 32-bit pattern.
///
/// Updating an existing value preserves its entry position and iterator validity.
/// Removing and reinserting a key moves its entry to the end. At most
/// `max(size(), 7)` removed entries can remain reachable pending compaction.
/// Iterators and member callbacks detect structural changes and trap with
/// `collection modified during iteration`.
/// @see [Std.Collections.IntMap](https://github.com/Kayyo321/neura/wiki/Stdlib-Collections-IntMap)
'''
int_factories = '''    /// @brief Creates an empty map with the default table size.
    export let create = func<V> -> IntMap<V> => new IntMap<V>(Seal.Token, hash_core.create());

    /// @brief Creates an empty map sized for `capacity` entries.
    /// Traps with `invalid argument: capacity` outside 0 through 2^29.
    export let with_capacity = func<V>(capacity: int) -> IntMap<V> =>
        new IntMap<V>(Seal.Token, hash_core.with_capacity(capacity));

'''
string_header = '''/// @brief Insertion-ordered hash maps with string keys.
/// Keys are compared byte for byte and hashed by folding their bytes from zero.
/// Each step multiplies the previous hash by 31 and adds the byte with 32-bit wraparound.
///
/// Updating an existing value preserves its entry position and iterator validity.
/// Removing and reinserting a key moves its entry to the end. At most
/// `max(size(), 7)` removed entries can remain reachable pending compaction.
/// Iterators and member callbacks detect structural changes and trap with
/// `collection modified during iteration`.
/// @see [Std.Collections.StringMap](https://github.com/Kayyo321/neura/wiki/Stdlib-Collections-StringMap)
'''
string_factories = int_factories.replace('IntMap', 'StringMap')
(collections / 'int-map.nra').write_text(variant('IntMap', 'int', int_header, int_factories))
(collections / 'string-map.nra').write_text(variant('StringMap', 'string', string_header, string_factories))
