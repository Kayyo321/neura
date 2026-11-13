// The NVM heap (src/nvm/heap.hpp) through its interface only: every
// operation on every entry kind, at boundary sizes, reference traversal,
// sweeping, and the cell accounting of each kind.

#include "ir-optimization/support.hpp"
#include "nvm/heap.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace neura::testing {
namespace {

using nvm::ElementWidth;
using nvm::EntryKind;
using nvm::Heap;

Heap::Ref live(const Heap& heap, std::uint64_t handle) {
    const auto ref = heap.find(handle);
    check(static_cast<bool>(ref), "handle " + std::to_string(handle) + " is live");
    return ref;
}

std::vector<std::uint64_t> elements_of(const Heap& heap, Heap::Ref ref) {
    std::vector<std::uint64_t> out;
    for (std::size_t i = 0; i < heap.length(ref); ++i) out.push_back(heap.element(ref, i));
    return out;
}

} // namespace

NEURA_TEST(heap, handles_count_up_and_are_never_reused) {
    Heap heap;
    check(!heap.find(0) && !heap.find(1) && heap.last_handle() == 0, "an empty heap names nothing");
    check_equal(heap.next_live(0), std::uint64_t{0}, "an empty heap has no live handle");
    const auto a = heap.create_object(7, 0);
    const auto b = heap.create_text(EntryKind::Text, 3, "x");
    check_equal(a, std::uint64_t{1}, "first handle");
    check_equal(b, std::uint64_t{2}, "second handle");
    heap.free(a);
    check(!heap.find(a), "a freed handle is not live");
    const auto c = heap.create_object(7, 1);
    check_equal(c, std::uint64_t{3}, "a freed handle is not reused");
    check_equal(heap.last_handle(), std::uint64_t{3}, "last handle");
    check(!heap.find(4) && !heap.find(std::numeric_limits<std::uint64_t>::max()), "unassigned handles");
    check_equal(heap.next_live(0), b, "first live handle");
    check_equal(heap.next_live(b), c, "next live handle");
    check_equal(heap.next_live(c), std::uint64_t{0}, "no live handle after the last");
}

NEURA_TEST(heap, pages_are_released_and_handles_stay_ordered) {
    // More handles than one page holds; free a whole page, then walk.
    Heap heap;
    std::vector<std::uint64_t> handles;
    for (int i = 0; i < 700; ++i) handles.push_back(heap.create_filled(EntryKind::Array, 1, ElementWidth::W8, 1, i));
    // Handles 1 to 255 fill the first page; 256 starts the second.
    for (std::size_t i = 0; i < 255; ++i) heap.free(handles[i]);
    heap.free(handles[300]);
    std::vector<std::uint64_t> walked;
    heap.for_each([&](std::uint64_t handle, Heap::Ref ref) {
        check_equal(heap.element(ref, 0), handle - 1, "entry of handle " + std::to_string(handle));
        walked.push_back(handle);
    });
    check_equal(walked.size(), std::size_t{444}, "live entries");
    check_equal(walked.front(), std::uint64_t{256}, "first live handle");
    check(std::find(walked.begin(), walked.end(), 301) == walked.end(), "a freed handle inside a page");
    std::uint64_t at = 0;
    for (const auto handle : walked) {
        at = heap.next_live(at);
        check_equal(at, handle, "next_live follows for_each");
    }
    check_equal(heap.next_live(at), std::uint64_t{0}, "after the last");
    check_equal(heap.create_object(1, 0), std::uint64_t{701}, "the next handle after a released page");
}

NEURA_TEST(heap, objects_track_each_field_and_its_flag) {
    Heap heap;
    const auto empty = live(heap, heap.create_object(2, 0));
    check_equal(heap.field_count(empty), std::size_t{0}, "no field");
    check(heap.leaf(empty), "an object without fields is a leaf");
    check_equal(heap.cells(empty), std::uint64_t{1}, "an empty object counts one cell");
    const auto ref = live(heap, heap.create_object(5, 3));
    check(heap.kind(ref) == EntryKind::Object, "kind");
    check_equal(heap.type(ref), std::uint32_t{5}, "layout");
    check(!heap.leaf(ref), "an object with fields is not a leaf");
    check_equal(heap.field_count(ref), std::size_t{3}, "field count");
    check_equal(heap.cells(ref), std::uint64_t{4}, "one cell per field and one for the object");
    check_equal(Heap::object_cells(3), std::uint64_t{4}, "object_cells");
    for (std::uint32_t f = 0; f < 3; ++f) check(!heap.initialized(ref, f), "a new field is uninitialized");
    heap.set_field(ref, 1, 0xffff'ffff'ffff'fff0ULL);
    check(heap.initialized(ref, 1) && !heap.initialized(ref, 0) && !heap.initialized(ref, 2), "only field 1 set");
    check_equal(heap.field(ref, 1), 0xffff'ffff'ffff'fff0ULL, "a field reads back its 64 bits");
    heap.set_field(ref, 2, 0);
    check(heap.initialized(ref, 2) && heap.field(ref, 2) == 0, "writing zero initializes");
    heap.restore_field(ref, 1, 9, false);
    check(!heap.initialized(ref, 1) && heap.field(ref, 1) == 9, "restore puts back value and flag");
    heap.restore_field(ref, 0, 4, true);
    check(heap.initialized(ref, 0) && heap.field(ref, 0) == 4, "restore can set the flag");
    check(heap.declared_width(ref) == ElementWidth::W8, "an object's declared width");
}

NEURA_TEST(heap, arrays_and_lists_hold_exact_bits) {
    Heap heap;
    const std::array<std::uint64_t, 3> values{0, 0x1'0000'0000ULL, std::numeric_limits<std::uint64_t>::max()};
    for (const auto kind : {EntryKind::Array, EntryKind::List}) {
        const auto ref = live(heap, heap.create_collection(kind, 9, ElementWidth::W2, values));
        check(heap.kind(ref) == kind && heap.type(ref) == 9, "kind and type");
        check(heap.declared_width(ref) == ElementWidth::W2, "declared width as passed");
        check(elements_of(heap, ref) == std::vector<std::uint64_t>(values.begin(), values.end()),
              "elements read back every bit, wider than the declared width included");
        heap.set_element(ref, 2, 7);
        check_equal(heap.element(ref, 2), std::uint64_t{7}, "set_element");
        check_equal(heap.element(ref, 1), std::uint64_t{0x1'0000'0000ULL}, "other elements unchanged");
        check_equal(heap.cells(ref), std::uint64_t{4}, "one cell per element");
        check_equal(heap.version(ref), std::uint64_t{0}, "a new collection's version");
    }
    const auto empty = live(heap, heap.create_collection(EntryKind::Array, 9, ElementWidth::W8, {}));
    check(heap.length(empty) == 0 && heap.leaf(empty) && heap.cells(empty) == 1, "an empty array");
    const auto filled = live(heap, heap.create_filled(EntryKind::List, 4, ElementWidth::W1, 5, 0x1ff));
    check(elements_of(heap, filled) == std::vector<std::uint64_t>(5, 0x1ff), "create_filled");
    check(heap.declared_width(filled) == ElementWidth::W1, "create_filled records the width");
    check_equal(Heap::collection_cells(5), std::uint64_t{6}, "collection_cells");
    check_equal(Heap::max_elements(), std::vector<std::uint64_t>().max_size(), "max_elements");
}

NEURA_TEST(heap, lists_grow_shrink_and_count_versions) {
    Heap heap;
    const auto ref = live(heap, heap.create_collection(EntryKind::List, 1, ElementWidth::W8, {}));
    // Across the growth boundaries of the storage.
    std::vector<std::uint64_t> expected;
    for (std::uint64_t i = 0; i < 70; ++i) {
        heap.append(ref, i * 3);
        expected.push_back(i * 3);
        check_equal(heap.length(ref), expected.size(), "length after append");
        check_equal(heap.version(ref), i + 1, "every append increments the version");
    }
    check(elements_of(heap, ref) == expected, "appended elements in order");
    heap.remove(ref, 0);
    heap.remove(ref, 68);
    heap.remove(ref, 10);
    expected.erase(expected.begin());
    expected.erase(expected.begin() + 68);
    expected.erase(expected.begin() + 10);
    check(elements_of(heap, ref) == expected, "removal moves later elements down");
    check_equal(heap.version(ref), std::uint64_t{73}, "every removal increments the version");
    check_equal(heap.cells(ref), std::uint64_t{1 + 67}, "cells follow the length");
    heap.advance_version(ref);
    check_equal(heap.version(ref), std::uint64_t{74}, "advance_version");
    check(elements_of(heap, ref) == expected, "advance_version changes no element");
    const auto one = live(heap, heap.create_collection(EntryKind::List, 1, ElementWidth::W8, {}));
    heap.append(one, 5);
    heap.remove(one, 0);
    check(heap.length(one) == 0 && heap.version(one) == 2 && heap.leaf(one), "a list back to empty");
}

NEURA_TEST(heap, text_and_bytes_are_bytes) {
    Heap heap;
    const std::string bytes("a\0\xff\x80z", 5);
    for (const auto kind : {EntryKind::Text, EntryKind::Bytes}) {
        const auto ref = live(heap, heap.create_text(kind, 6, bytes));
        check(heap.kind(ref) == kind, "kind");
        check_equal(std::string(heap.text(ref)), bytes, "text view");
        check_equal(heap.length(ref), std::size_t{5}, "length in bytes");
        check_equal(heap.element(ref, 2), std::uint64_t{0xff}, "a byte reads zero-extended");
        check_equal(heap.element(ref, 1), std::uint64_t{0}, "a zero byte");
        check_equal(heap.cells(ref), std::uint64_t{6}, "one cell per byte");
        check(heap.leaf(ref), "text holds no reference");
        check(heap.declared_width(ref) == ElementWidth::W1, "text is one byte wide");
        const auto view = heap.elements(ref);
        check(view.width == ElementWidth::W1 && view.length == 5 && std::memcmp(view.data, bytes.data(), 5) == 0,
              "elements() of text views its bytes");
        std::string out(5, '?');
        heap.copy_out(ref, out.data(), ElementWidth::W1);
        check_equal(out, bytes, "copy_out of text");
        const unsigned char in[2] = {1, 2};
        check_equal(heap.copy_in(ref, in, 2, ElementWidth::W1), std::size_t{0}, "copy_in leaves text alone");
        check_equal(std::string(heap.text(ref)), bytes, "text unchanged by copy_in");
    }
    const auto empty = live(heap, heap.create_text(EntryKind::Text, 6, ""));
    check(heap.text(empty).empty() && heap.cells(empty) == 1, "empty text counts one cell");
    check_equal(Heap::text_cells(5), std::uint64_t{6}, "text_cells");
}

NEURA_TEST(heap, bulk_copies_truncate_and_zero_extend) {
    Heap heap;
    const std::array<std::uint64_t, 3> values{0x0102'0304'0506'0708ULL, 0xffff'ffff'ffff'ffffULL, 0x80};
    const auto ref = live(heap, heap.create_collection(EntryKind::Array, 1, ElementWidth::W8, values));
    const auto view = heap.elements(ref);
    check(view.length == 3 && view.width == ElementWidth::W8, "view length and storage width");
    std::uint64_t first = 0;
    std::memcpy(&first, view.data, 8);
    check_equal(first, values[0], "the view holds the elements in place");
    for (const auto width : {ElementWidth::W1, ElementWidth::W2, ElementWidth::W4, ElementWidth::W8}) {
        const auto w = static_cast<std::size_t>(width);
        std::vector<unsigned char> out(3 * w + 1, 0xee);
        heap.copy_out(ref, out.data(), width);
        check_equal(static_cast<int>(out[3 * w]), 0xee, "copy_out writes length * width bytes");
        for (std::size_t i = 0; i < 3; ++i) {
            std::uint64_t got = 0;
            std::memcpy(&got, out.data() + i * w, w);
            const auto mask = w == 8 ? ~std::uint64_t{0} : (std::uint64_t{1} << (8 * w)) - 1;
            check_equal(got, values[i] & mask, "copy_out keeps the low bytes");
        }
    }
    // copy_in: zero-extends, stores min(length, count).
    const std::array<std::uint16_t, 2> narrow{0xfffe, 0x0001};
    check_equal(heap.copy_in(ref, narrow.data(), 2, ElementWidth::W2), std::size_t{2}, "copy_in count");
    check(elements_of(heap, ref) == std::vector<std::uint64_t>{0xfffe, 1, 0x80}, "copy_in zero-extends, keeps the rest");
    const std::array<std::uint32_t, 5> wide{0x8000'0001U, 2, 3, 4, 5};
    check_equal(heap.copy_in(ref, wide.data(), 5, ElementWidth::W4), std::size_t{3}, "copy_in stops at the length");
    check(elements_of(heap, ref) == std::vector<std::uint64_t>{0x8000'0001U, 2, 3}, "copy_in W4 zero-extends");
    const std::array<std::uint8_t, 1> byte{0x9c};
    check_equal(heap.copy_in(ref, byte.data(), 1, ElementWidth::W1), std::size_t{1}, "copy_in W1");
    check(elements_of(heap, ref) == std::vector<std::uint64_t>{0x9c, 2, 3}, "copy_in W1 element");
    const std::array<std::uint64_t, 1> full{0xdead'beef'0000'0001ULL};
    heap.copy_in(ref, full.data(), 1, ElementWidth::W8);
    check_equal(heap.element(ref, 0), full[0], "copy_in W8");
    const auto empty = live(heap, heap.create_collection(EntryKind::List, 1, ElementWidth::W8, {}));
    std::uint64_t untouched = 5;
    heap.copy_out(empty, &untouched, ElementWidth::W8);
    check_equal(untouched, std::uint64_t{5}, "copy_out of an empty list writes nothing");
    check_equal(heap.copy_in(empty, full.data(), 1, ElementWidth::W8), std::size_t{0}, "copy_in into an empty list");
}

NEURA_TEST(heap, identity_is_assigned_once_in_request_order) {
    Heap heap;
    const auto a = live(heap, heap.create_object(1, 0));
    const auto b = live(heap, heap.create_object(1, 0));
    check_equal(heap.identity(a), std::uint64_t{0}, "no identity before a request");
    check_equal(heap.last_identity(), std::uint64_t{0}, "no identity given yet");
    check_equal(heap.ensure_identity(b), std::uint64_t{1}, "first request");
    check_equal(heap.ensure_identity(a), std::uint64_t{2}, "second request");
    check_equal(heap.ensure_identity(b), std::uint64_t{1}, "a second request keeps the number");
    check_equal(heap.identity(a), std::uint64_t{2}, "identity reads the number");
    check_equal(heap.last_identity(), std::uint64_t{2}, "the last identity given");
}

NEURA_TEST(heap, traversal_visits_references_by_plan_and_flag) {
    Heap heap;
    const auto target = heap.create_object(1, 0);
    const auto freed = heap.create_object(1, 0);
    heap.free(freed);
    const auto object = live(heap, heap.create_object(2, 4));
    heap.set_field(object, 0, 0);       // nil
    heap.set_field(object, 1, target);  // live
    heap.set_field(object, 2, freed);   // freed
    heap.restore_field(object, 3, 99, false);  // uninitialized, but holds bits
    std::vector<std::uint64_t> seen;
    const std::array<std::size_t, 4> plan{0, 1, 2, 3};
    heap.visit_fields(object, plan, [&](std::uint64_t bits) { seen.push_back(bits); });
    check(seen == std::vector<std::uint64_t>{0, target, freed}, "initialized planned fields, in order");
    seen.clear();
    const std::array<std::size_t, 1> only{2};
    heap.visit_fields(object, only, [&](std::uint64_t bits) { seen.push_back(bits); });
    check(seen == std::vector<std::uint64_t>{freed}, "only planned fields");
    const std::array<std::uint64_t, 3> refs{target, 0, freed};
    const auto list = live(heap, heap.create_collection(EntryKind::List, 3, ElementWidth::W8, refs));
    seen.clear();
    heap.visit_elements(list, [&](std::uint64_t bits) { seen.push_back(bits); });
    check(seen == std::vector<std::uint64_t>(refs.begin(), refs.end()), "every element in order");

    auto builder = Heap::Builder::object(2, 3);
    builder.set_field(2, target);
    seen.clear();
    builder.visit_fields(std::span<const std::size_t>(plan.data(), 3), [&](std::uint64_t bits) { seen.push_back(bits); });
    check(seen == std::vector<std::uint64_t>{target}, "a builder visits its initialized fields");
    auto collection = Heap::Builder::collection(EntryKind::Array, 3, ElementWidth::W8);
    collection.push(target);
    collection.push(0);
    seen.clear();
    collection.visit_elements([&](std::uint64_t bits) { seen.push_back(bits); });
    check(seen == std::vector<std::uint64_t>{target, 0}, "a builder visits its elements");
    check(collection.references().size() == 2 && collection.references()[0] == target, "builder references");
}

NEURA_TEST(heap, sweep_frees_unmarked_entries_in_handle_order) {
    Heap heap;
    std::vector<std::uint64_t> handles;
    handles.push_back(heap.create_object(1, 2));                                      // 3 cells
    handles.push_back(heap.create_text(EntryKind::Text, 2, "abcd"));                   // 5 cells
    handles.push_back(heap.create_filled(EntryKind::List, 3, ElementWidth::W8, 6, 0));  // 7 cells
    handles.push_back(heap.create_object(1, 0));                                      // 1 cell
    check(heap.mark(live(heap, handles[1])), "first mark");
    check(!heap.mark(live(heap, handles[1])), "second mark of the same entry");
    check(heap.mark(live(heap, handles[3])), "mark");
    check(heap.marked(live(heap, handles[1])) && !heap.marked(live(heap, handles[0])), "marked");
    std::vector<std::pair<std::uint64_t, std::uint64_t>> dropped;
    heap.sweep([&](std::uint64_t handle, std::uint64_t cells) { dropped.emplace_back(handle, cells); });
    check(dropped == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{handles[0], 3}, {handles[2], 7}},
          "unmarked entries with their cells, in handle order");
    check(!heap.find(handles[0]) && !heap.find(handles[2]), "dropped entries are gone");
    check(!heap.marked(live(heap, handles[1])) && !heap.marked(live(heap, handles[3])), "survivors are unmarked");
    dropped.clear();
    heap.sweep([&](std::uint64_t handle, std::uint64_t cells) { dropped.emplace_back(handle, cells); });
    check(dropped.size() == 2, "a sweep without marks frees everything");
    check(!heap.find(handles[1]) && !heap.find(handles[3]) && heap.next_live(0) == 0, "heap empty");
}

NEURA_TEST(heap, builders_create_what_they_hold) {
    Heap heap;
    auto object = Heap::Builder::object(4, 2);
    check_equal(object.cells(), std::uint64_t{3}, "builder object cells");
    object.set_field(1, 77);
    const auto o = live(heap, heap.create(std::move(object)));
    check(heap.kind(o) == EntryKind::Object && heap.type(o) == 4, "object kind and layout");
    check(!heap.initialized(o, 0) && heap.initialized(o, 1) && heap.field(o, 1) == 77, "builder fields");

    auto list = Heap::Builder::collection(EntryKind::List, 8, ElementWidth::W1);
    list.reserve(4);
    list.push(300);
    const unsigned char raw[3] = {0, 0x80, 0xff};
    list.push_bytes(raw, 3);
    check_equal(list.length(), std::size_t{4}, "builder length");
    check_equal(list.cells(), std::uint64_t{5}, "builder list cells");
    const auto l = live(heap, heap.create(std::move(list)));
    check(elements_of(heap, l) == std::vector<std::uint64_t>{300, 0, 0x80, 0xff}, "builder elements");
    check(heap.declared_width(l) == ElementWidth::W1 && heap.version(l) == 0, "builder list width and version");

    auto text = Heap::Builder::text(EntryKind::Bytes, 5);
    text.reserve_bytes(8);
    text.append("ab");
    text.push_byte(0xff);
    text.append_elements(heap, l);
    check_equal(text.size(), std::size_t{7}, "builder text size");
    check_equal(std::string(text.bytes()), std::string("ab\xff\x2c\x00\x80\xff", 7), "elements append their low byte");
    check_equal(text.cells(), std::uint64_t{8}, "builder text cells");
    const auto t = live(heap, heap.create(std::move(text)));
    check(heap.kind(t) == EntryKind::Bytes && heap.type(t) == 5 && heap.text(t).size() == 7, "bytes entry");

    auto cleared = Heap::Builder::text(EntryKind::Text, 5);
    cleared.append("xyz");
    cleared.clear_bytes();
    check(cleared.bytes().empty() && cleared.cells() == 1, "clear_bytes");
    check(heap.text(live(heap, heap.create(std::move(cleared)))).empty(), "an emptied text");
}

NEURA_TEST(heap, same_compares_everything_but_marks) {
    Heap a;
    Heap b;
    const auto make = [](Heap& heap) {
        const auto o = heap.create_object(3, 2);
        heap.set_field(heap.find(o), 0, 5);
        const std::array<std::uint64_t, 2> values{1, 2};
        const auto l = heap.create_collection(EntryKind::List, 4, ElementWidth::W8, values);
        const auto t = heap.create_text(EntryKind::Text, 5, "hi");
        return std::array<std::uint64_t, 3>{o, l, t};
    };
    const auto x = make(a);
    const auto y = make(b);
    for (std::size_t i = 0; i < 3; ++i) check(Heap::same(a, a.find(x[i]), b, b.find(y[i])), "equal entries");
    // An uninitialized field's bits do not count.
    b.restore_field(b.find(y[0]), 1, 9, false);
    check(Heap::same(a, a.find(x[0]), b, b.find(y[0])), "uninitialized bits are ignored");
    b.restore_field(b.find(y[0]), 1, 0, true);
    check(!Heap::same(a, a.find(x[0]), b, b.find(y[0])), "a field's flag counts");
    b.restore_field(b.find(y[0]), 1, 0, false);
    b.set_field(b.find(y[0]), 0, 6);
    check(!Heap::same(a, a.find(x[0]), b, b.find(y[0])), "a field's bits count");
    b.set_field(b.find(y[0]), 0, 5);
    b.mark(b.find(y[0]));
    check(Heap::same(a, a.find(x[0]), b, b.find(y[0])), "marks do not count");
    b.advance_version(b.find(y[1]));
    check(!Heap::same(a, a.find(x[1]), b, b.find(y[1])), "the version counts");
    a.advance_version(a.find(x[1]));
    a.set_element(a.find(x[1]), 1, 3);
    check(!Heap::same(a, a.find(x[1]), b, b.find(y[1])), "elements count");
    b.ensure_identity(b.find(y[0]));
    check(!Heap::same(a, a.find(x[0]), b, b.find(y[0])), "identity counts");
    check(!Heap::same(a, a.find(x[2]), b, b.find(y[1])), "kind counts");
    const auto other = b.create_text(EntryKind::Text, 5, "ho");
    check(!Heap::same(a, a.find(x[2]), b, b.find(other)), "bytes count");
    const auto typed = b.create_text(EntryKind::Text, 6, "hi");
    check(!Heap::same(a, a.find(x[2]), b, b.find(typed)), "type counts");
}

NEURA_TEST(heap, element_widths_follow_the_element_type) {
    using Kind = nvr::TypeKind;
    check(nvm::element_width({Kind::Bool}) == ElementWidth::W1, "bool");
    check(nvm::element_width({Kind::Unsigned, 8}) == ElementWidth::W1, "uint8");
    check(nvm::element_width({Kind::Signed, 16}) == ElementWidth::W2, "int16");
    check(nvm::element_width({Kind::BFloat, 16}) == ElementWidth::W2, "bfloat16");
    check(nvm::element_width({Kind::Float, 32}) == ElementWidth::W4, "float32");
    check(nvm::element_width({Kind::Signed, 32}) == ElementWidth::W4, "int32");
    check(nvm::element_width({Kind::Unsigned, 64}) == ElementWidth::W8, "uint64");
    check(nvm::element_width({Kind::Float, 64}) == ElementWidth::W8, "float64");
    check(nvm::element_width({Kind::Text}) == ElementWidth::W8, "text reference");
    check(nvm::element_width({Kind::Object, 0, 1}) == ElementWidth::W8, "object reference");
    check(nvm::element_width({Kind::List, 0, 1}) == ElementWidth::W8, "list reference");
}

} // namespace neura::testing
