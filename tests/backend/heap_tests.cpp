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

// --- Storage: elements at their declared width, widening, block reuse ------

namespace {

constexpr std::array<ElementWidth, 4> all_widths{ElementWidth::W1, ElementWidth::W2, ElementWidth::W4,
                                                 ElementWidth::W8};
constexpr std::array<ElementWidth, 3> narrow_widths{ElementWidth::W1, ElementWidth::W2, ElementWidth::W4};

std::size_t bytes_of(ElementWidth width) { return static_cast<std::size_t>(width); }

std::uint64_t mask_of(ElementWidth width) {
    return width == ElementWidth::W8 ? ~std::uint64_t{0} : (std::uint64_t{1} << (8 * bytes_of(width))) - 1;
}

std::string label(EntryKind kind, ElementWidth width, std::size_t length = 0) {
    return std::string(kind == EntryKind::List ? "list" : "array") + " W" + std::to_string(bytes_of(width)) +
           " length " + std::to_string(length);
}

// `count` values that fit `width`, with every bit of the width used somewhere
// (the top bit included) and the largest value at the ends.
std::vector<std::uint64_t> narrow_values(std::size_t count, ElementWidth width, std::uint64_t seed = 1) {
    std::vector<std::uint64_t> out;
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(((i + seed) * 0x9e37'79b9'7f4a'7c15ULL ^ (i << 7)) & mask_of(width));
    }
    if (count != 0) out.front() = mask_of(width);
    if (count > 1) out.back() = mask_of(width) ^ 1;
    return out;
}

// The values as `width`-byte elements in host byte order, low bytes kept.
std::vector<unsigned char> packed(const std::vector<std::uint64_t>& values, ElementWidth width) {
    std::vector<unsigned char> out(values.size() * bytes_of(width));
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto v = values[i];
        unsigned char* at = out.data() + i * bytes_of(width);
        switch (width) {
        case ElementWidth::W1: { const auto x = static_cast<std::uint8_t>(v); std::memcpy(at, &x, 1); break; }
        case ElementWidth::W2: { const auto x = static_cast<std::uint16_t>(v); std::memcpy(at, &x, 2); break; }
        case ElementWidth::W4: { const auto x = static_cast<std::uint32_t>(v); std::memcpy(at, &x, 4); break; }
        case ElementWidth::W8: std::memcpy(at, &v, 8); break;
        }
    }
    return out;
}

std::vector<std::uint64_t> masked(std::vector<std::uint64_t> values, ElementWidth width) {
    for (auto& v : values) v &= mask_of(width);
    return values;
}

// The view's bytes and width, checked against the values stored at `width`.
void check_view(const Heap& heap, Heap::Ref ref, const std::vector<std::uint64_t>& values, ElementWidth width,
                const std::string& what) {
    const auto view = heap.elements(ref);
    check(view.width == width, what + ": view width");
    check_equal(view.length, values.size(), what + ": view length");
    const auto expected = packed(values, width);
    check(expected.empty() || std::memcmp(view.data, expected.data(), expected.size()) == 0,
          what + ": the view holds the elements in host byte order at the storage width");
}

// Lengths around the block-size boundaries of an array of `width` (a 24-byte
// header and the elements) and of a list's buffer, and the first lengths.
std::vector<std::size_t> boundary_lengths(ElementWidth width) {
    std::vector<std::size_t> lengths{0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33};
    const auto w = bytes_of(width);
    for (const std::size_t block : {32, 64, 128, 136, 256, 272, 512, 1024, 2048, 2056, 65536}) {
        for (const auto n : {block / w, block >= 24 ? (block - 24) / w : 0}) {
            for (const auto m : {n == 0 ? n : n - 1, n, n + 1}) {
                if (std::find(lengths.begin(), lengths.end(), m) == lengths.end()) lengths.push_back(m);
            }
        }
    }
    std::sort(lengths.begin(), lengths.end());
    return lengths;
}

// A value with one bit above `width`.
std::uint64_t one_above(ElementWidth width) { return std::uint64_t{1} << (8 * bytes_of(width)); }

} // namespace

NEURA_TEST(heap, collections_hold_every_width_at_boundary_lengths) {
    for (const auto kind : {EntryKind::Array, EntryKind::List}) {
        for (const auto width : all_widths) {
            Heap heap;
            Heap wide;
            for (const auto length : boundary_lengths(width)) {
                const auto what = label(kind, width, length);
                const auto values = narrow_values(length, width);
                const auto handle = heap.create_collection(kind, 9, width, values);
                const auto ref = live(heap, handle);
                check(heap.kind(ref) == kind && heap.type(ref) == 9, what + ": kind and type");
                check(heap.declared_width(ref) == width, what + ": declared width");
                check_equal(heap.length(ref), length, what + ": length");
                check_equal(heap.cells(ref), std::uint64_t{length + 1}, what + ": one cell per element");
                check_equal(heap.leaf(ref), length == 0, what + ": leaf");
                check(elements_of(heap, ref) == values, what + ": elements");
                check_view(heap, ref, values, width, what);
                std::vector<std::uint64_t> visited;
                heap.visit_elements(ref, [&](std::uint64_t bits) { visited.push_back(bits); });
                check(visited == values, what + ": visit_elements");
                for (const auto out_width : all_widths) {
                    std::vector<unsigned char> out(length * bytes_of(out_width) + 1, 0xee);
                    heap.copy_out(ref, out.data(), out_width);
                    check_equal(static_cast<int>(out.back()), 0xee, what + ": copy_out stops at the end");
                    out.pop_back();
                    check(out == packed(masked(values, out_width), out_width),
                          what + ": copy_out at W" + std::to_string(bytes_of(out_width)));
                }
                // An equal collection stored at eight bytes, in another heap.
                const auto other = live(wide, wide.create_collection(kind, 9, ElementWidth::W8, values));
                check(Heap::same(heap, ref, wide, other), what + ": same() across storage widths");
                // The builder makes the same entry.
                auto builder = Heap::Builder::collection(kind, 9, width);
                for (const auto v : values) builder.push(v);
                const auto built = live(heap, heap.create(std::move(builder)));
                check(Heap::same(heap, ref, heap, built) && heap.declared_width(built) == width, what + ": builder");
                check_view(heap, built, values, width, what + " (built)");
                if (length == 0) continue;
                // Writers that keep the width.
                const auto replacement = narrow_values(length, width, 7);
                const auto in = packed(replacement, width);
                check_equal(heap.copy_in(ref, in.data(), length, width), length, what + ": copy_in count");
                check(elements_of(heap, ref) == replacement, what + ": copy_in at the storage width");
                const auto bytes = packed(masked(values, ElementWidth::W1), ElementWidth::W1);
                heap.copy_in(ref, bytes.data(), length, ElementWidth::W1);
                check(elements_of(heap, ref) == masked(values, ElementWidth::W1), what + ": copy_in zero-extends");
                heap.set_element(ref, 0, 0);
                heap.set_element(ref, length - 1, mask_of(width));
                check(heap.element(ref, length - 1) == mask_of(width) && (length == 1 || heap.element(ref, 0) == 0),
                      what + ": set_element");
                check(heap.elements(ref).width == width, what + ": narrow writes keep the storage width");
                check(heap.find(handle) == ref, what + ": the Ref still names the entry");
                const auto filled = live(heap, heap.create_filled(kind, 9, width, length, mask_of(width)));
                check(elements_of(heap, filled) == std::vector<std::uint64_t>(length, mask_of(width)), what + ": create_filled");
                check(heap.elements(filled).width == width && heap.cells(filled) == length + 1, what + ": create_filled width");
            }
        }
    }
}

NEURA_TEST(heap, lists_grow_and_shrink_at_every_width) {
    for (const auto width : all_widths) {
        Heap heap;
        const auto handle = heap.create_collection(EntryKind::List, 3, width, {});
        const auto ref = live(heap, handle);
        // Across every growth step of the buffer up to 600 elements.
        const auto values = narrow_values(600, width);
        std::vector<std::uint64_t> expected;
        for (std::size_t i = 0; i < values.size(); ++i) {
            heap.append(ref, values[i]);
            expected.push_back(values[i]);
            check_equal(heap.length(ref), expected.size(), label(EntryKind::List, width, i) + ": length");
            check_equal(heap.element(ref, i), values[i], label(EntryKind::List, width, i) + ": appended element");
            check_equal(heap.element(ref, 0), values[0], label(EntryKind::List, width, i) + ": first element kept");
            if ((i & (i + 1)) == 0 || i % 37 == 0) check(elements_of(heap, ref) == expected, label(EntryKind::List, width, i));
        }
        check(heap.find(handle) == ref, "growth keeps the Ref");
        check_view(heap, ref, expected, width, label(EntryKind::List, width, 600));
        check_equal(heap.version(ref), std::uint64_t{600}, "versions");
        for (const std::size_t index : {std::size_t{599}, std::size_t{0}, std::size_t{300}, std::size_t{1}}) {
            heap.remove(ref, index);
            expected.erase(expected.begin() + static_cast<std::ptrdiff_t>(index));
            check(elements_of(heap, ref) == expected, label(EntryKind::List, width) + ": remove " + std::to_string(index));
        }
        while (heap.length(ref) != 0) heap.remove(ref, heap.length(ref) - 1);
        check(heap.leaf(ref) && heap.cells(ref) == 1 && heap.version(ref) == 600 + 4 + 596, "emptied list");
        heap.append(ref, values[3]);
        check(elements_of(heap, ref) == std::vector<std::uint64_t>{values[3]}, "append after emptying");
        // A list created with elements grows from there.
        const auto made = live(heap, heap.create_collection(EntryKind::List, 3, width, narrow_values(5, width)));
        auto more = narrow_values(5, width);
        for (std::size_t i = 0; i < 40; ++i) {
            heap.append(made, values[i]);
            more.push_back(values[i]);
        }
        check(elements_of(heap, made) == more, label(EntryKind::List, width) + ": growth of a created list");
        const auto repeated = live(heap, heap.create_filled(EntryKind::List, 3, width, 3, 1));
        heap.append(repeated, 2);
        check(elements_of(heap, repeated) == std::vector<std::uint64_t>{1, 1, 1, 2}, "growth of a filled list");
    }
}

NEURA_TEST(heap, objects_keep_initialized_bits_across_words) {
    for (const std::size_t count : {0, 1, 63, 64, 65, 127, 128, 129, 200}) {
        const auto what = "object of " + std::to_string(count) + " fields";
        Heap heap;
        const auto ref = live(heap, heap.create_object(4, count));
        check_equal(heap.field_count(ref), count, what + ": field count");
        check_equal(heap.cells(ref), std::uint64_t{count + 1}, what + ": cells");
        check_equal(heap.leaf(ref), count == 0, what + ": leaf");
        for (std::uint32_t f = 0; f < count; ++f) {
            check(!heap.initialized(ref, f) && heap.field(ref, f) == 0, what + ": a new field is uninitialized and zero");
        }
        // Every third field, and both sides of each word boundary.
        const auto chosen = [](std::uint32_t f) { return f % 3 == 0 || f % 64 == 63 || f % 64 == 0; };
        auto builder = Heap::Builder::object(4, count);
        std::vector<std::size_t> plan;
        std::vector<std::uint64_t> expected;
        for (std::uint32_t f = 0; f < count; ++f) {
            plan.push_back(f);
            const auto bits = (std::uint64_t{f} << 56) | 0x00ff'0000'0000'0001ULL | f;
            if (!chosen(f)) continue;
            heap.set_field(ref, f, bits);
            builder.set_field(f, bits);
            expected.push_back(bits);
        }
        for (std::uint32_t f = 0; f < count; ++f) {
            check_equal(heap.initialized(ref, f), chosen(f), what + ": flag of field " + std::to_string(f));
            if (chosen(f)) check_equal(heap.field(ref, f), (std::uint64_t{f} << 56) | 0x00ff'0000'0000'0001ULL | f, what + ": field bits");
        }
        std::vector<std::uint64_t> visited;
        heap.visit_fields(ref, plan, [&](std::uint64_t bits) { visited.push_back(bits); });
        check(visited == expected, what + ": visit_fields");
        const auto built = live(heap, heap.create(std::move(builder)));
        check(Heap::same(heap, ref, heap, built), what + ": the builder makes the same object");
        if (count == 0) continue;
        const auto last = static_cast<std::uint32_t>(count - 1);
        heap.restore_field(built, last, 5, !chosen(last));
        check(!Heap::same(heap, ref, heap, built), what + ": the last field's flag counts");
        heap.restore_field(built, last, heap.field(ref, last), chosen(last));
        check(Heap::same(heap, ref, heap, built), what + ": restored");
        for (std::uint32_t f = 0; f < count; ++f) heap.restore_field(ref, f, 0, false);
        for (std::uint32_t f = 0; f < count; ++f) check(!heap.initialized(ref, f), what + ": restore clears every flag");
        visited.clear();
        heap.visit_fields(ref, plan, [&](std::uint64_t bits) { visited.push_back(bits); });
        check(visited.empty(), what + ": nothing initialized to visit");
    }
}

NEURA_TEST(heap, a_value_wider_than_the_storage_widens_the_collection) {
    // Each narrow width, each writer, with one bit above the width and with
    // every bit set. A Boolean collection is a W1 collection to the heap.
    enum class Writer { SetElement, Append, CopyIn8, CopyInWider, Collection, Filled, Builder };
    for (const auto width : narrow_widths) {
        for (const auto wide : {one_above(width), ~std::uint64_t{0}}) {
            for (const auto writer : {Writer::SetElement, Writer::Append, Writer::CopyIn8, Writer::CopyInWider,
                                      Writer::Collection, Writer::Filled, Writer::Builder}) {
                for (const auto kind : {EntryKind::Array, EntryKind::List}) {
                    if (writer == Writer::Append && kind != EntryKind::List) continue;
                    if (writer == Writer::CopyInWider && wide == ~std::uint64_t{0}) continue;
                    const auto what = label(kind, width) + " writer " + std::to_string(static_cast<int>(writer)) +
                                      " value " + std::to_string(wide);
                    Heap heap;
                    auto expected = narrow_values(5, width);
                    std::uint64_t handle = 0;
                    switch (writer) {
                    case Writer::Collection:
                        expected[2] = wide;
                        handle = heap.create_collection(kind, 2, width, expected);
                        break;
                    case Writer::Filled:
                        expected.assign(5, wide);
                        handle = heap.create_filled(kind, 2, width, 5, wide);
                        break;
                    case Writer::Builder: {
                        expected[4] = wide;
                        auto builder = Heap::Builder::collection(kind, 2, width);
                        for (const auto v : expected) builder.push(v);
                        handle = heap.create(std::move(builder));
                        break;
                    }
                    default:
                        handle = heap.create_collection(kind, 2, width, expected);
                        break;
                    }
                    const auto ref = live(heap, handle);
                    const auto before = heap.cells(ref);
                    if (writer == Writer::SetElement || writer == Writer::Append || writer == Writer::CopyIn8 ||
                        writer == Writer::CopyInWider) {
                        check_view(heap, ref, expected, width, what + ": before");
                    }
                    switch (writer) {
                    case Writer::SetElement:
                        heap.set_element(ref, 3, wide);
                        expected[3] = wide;
                        break;
                    case Writer::Append:
                        heap.append(ref, wide);
                        expected.push_back(wide);
                        break;
                    case Writer::CopyIn8: {
                        auto in = expected;
                        in[1] = wide;
                        heap.copy_in(ref, in.data(), in.size(), ElementWidth::W8);
                        expected = in;
                        break;
                    }
                    case Writer::CopyInWider: {
                        const auto wider = static_cast<ElementWidth>(bytes_of(width) * 2);
                        auto values = expected;
                        values[0] = wide;
                        const auto in = packed(values, wider);
                        heap.copy_in(ref, in.data(), values.size(), wider);
                        expected = values;
                        break;
                    }
                    default:
                        break;
                    }
                    check(elements_of(heap, ref) == expected, what + ": every element reads back exactly");
                    check_view(heap, ref, expected, ElementWidth::W8, what + ": widened");
                    check(heap.declared_width(ref) == width, what + ": the declared width stays");
                    check(heap.find(handle) == ref, what + ": the Ref and the handle stay valid");
                    const auto added = writer == Writer::Append ? 1U : 0U;
                    check_equal(heap.cells(ref), before + added, what + ": the cells do not change");
                    // It stays wide; a narrow value is still exact.
                    heap.set_element(ref, 0, 1);
                    expected[0] = 1;
                    check(elements_of(heap, ref) == expected && heap.elements(ref).width == ElementWidth::W8,
                          what + ": a later narrow value");
                    if (kind == EntryKind::List) {
                        for (std::uint64_t i = 0; i < 20; ++i) {
                            heap.append(ref, i);
                            expected.push_back(i);
                        }
                        heap.remove(ref, 2);
                        expected.erase(expected.begin() + 2);
                        check(elements_of(heap, ref) == expected, what + ": growth and removal after widening");
                    }
                    // A narrow collection with equal elements is the same.
                    for (std::size_t i = 0; i < expected.size(); ++i) expected[i] &= mask_of(width);
                    for (std::size_t i = 0; i < expected.size(); ++i) heap.set_element(ref, i, expected[i]);
                    const auto narrow = live(heap, heap.create_collection(kind, 2, width, expected));
                    check(heap.elements(narrow).width == width, what + ": the comparison partner is narrow");
                    if (kind == EntryKind::List) {
                        while (heap.version(narrow) != heap.version(ref)) heap.advance_version(narrow);
                    }
                    check(Heap::same(heap, ref, heap, narrow) && Heap::same(heap, narrow, heap, ref),
                          what + ": same() ignores the storage width");
                    std::vector<unsigned char> out(expected.size() * bytes_of(width));
                    heap.copy_out(ref, out.data(), width);
                    check(out == packed(expected, width), what + ": copy_out of a widened collection");
                }
            }
        }
    }
}

NEURA_TEST(heap, a_view_shows_the_storage_at_the_time_it_is_taken) {
    // A view taken before a widening store must not be used after it; a
    // fresh view shows the eight-byte storage.
    for (const auto width : narrow_widths) {
        for (const auto kind : {EntryKind::Array, EntryKind::List}) {
            Heap heap;
            auto values = narrow_values(9, width);
            const auto ref = live(heap, heap.create_collection(kind, 1, width, values));
            check_view(heap, ref, values, width, label(kind, width) + ": before");
            heap.set_element(ref, 8, ~std::uint64_t{0});
            values[8] = ~std::uint64_t{0};
            check_view(heap, ref, values, ElementWidth::W8, label(kind, width) + ": a fresh view after widening");
        }
    }
    Heap heap;
    const auto text = live(heap, heap.create_text(EntryKind::Bytes, 1, std::string("\x00\xff\x80", 3)));
    const auto view = heap.elements(text);
    check(view.width == ElementWidth::W1 && view.length == 3 && std::memcmp(view.data, "\x00\xff\x80", 3) == 0,
          "a bytes entry is viewed at one byte");
}

NEURA_TEST(heap, reused_blocks_start_as_created) {
    // Fill entries of many block sizes with set bits, drop them by free()
    // and by a sweep, and create the same shapes again: nothing of the old
    // contents shows.
    Heap heap;
    const auto make_round = [&](bool dirty) {
        std::vector<std::uint64_t> handles;
        for (std::size_t fields = 0; fields <= 40; ++fields) {
            const auto h = heap.create_object(1, fields);
            handles.push_back(h);
            const auto ref = live(heap, h);
            if (dirty) {
                for (std::uint32_t f = 0; f < fields; ++f) heap.set_field(ref, f, ~std::uint64_t{0});
                heap.ensure_identity(ref);
                heap.mark(ref);
            } else {
                check(heap.identity(ref) == 0 && !heap.marked(ref), "a reused object has no identity or mark");
                for (std::uint32_t f = 0; f < fields; ++f) {
                    check(!heap.initialized(ref, f) && heap.field(ref, f) == 0, "a reused object's fields are new");
                }
            }
        }
        for (const auto width : all_widths) {
            for (std::size_t length = 0; length <= 40; ++length) {
                for (const auto kind : {EntryKind::Array, EntryKind::List}) {
                    const auto values = narrow_values(length, width, length);
                    const auto h = heap.create_collection(kind, 1, width, values);
                    handles.push_back(h);
                    const auto ref = live(heap, h);
                    if (dirty) {
                        for (std::size_t i = 0; i < length; ++i) heap.set_element(ref, i, mask_of(width));
                        if (kind == EntryKind::List) heap.append(ref, 1);
                    } else {
                        check(elements_of(heap, ref) == values && heap.version(ref) == 0 &&
                                  heap.elements(ref).width == width,
                              "a reused collection holds what it was created with");
                    }
                    const auto f = heap.create_filled(kind, 1, width, length, 0);
                    handles.push_back(f);
                    if (dirty) {
                        for (std::size_t i = 0; i < length; ++i) heap.set_element(live(heap, f), i, mask_of(width));
                    } else {
                        check(elements_of(heap, live(heap, f)) == std::vector<std::uint64_t>(length, 0),
                              "a reused filled collection is filled");
                    }
                }
            }
        }
        for (std::size_t length = 0; length <= 300; length += 13) {
            const std::string bytes(length, dirty ? '\xff' : 'a');
            const auto h = heap.create_text(EntryKind::Text, 1, bytes);
            handles.push_back(h);
            check_equal(std::string(heap.text(live(heap, h))), bytes, "text in a reused block");
        }
        return handles;
    };
    for (int round = 0; round < 3; ++round) {
        const auto dirty = make_round(true);
        // Half by free(), the rest by a sweep with nothing marked but what
        // make_round marked (objects), whose marks the first sweep clears.
        for (std::size_t i = 0; i < dirty.size(); i += 2) heap.free(dirty[i]);
        heap.sweep([](std::uint64_t, std::uint64_t) {});
        heap.sweep([](std::uint64_t, std::uint64_t) {});
        check_equal(heap.next_live(0), std::uint64_t{0}, "every entry dropped");
        make_round(false);
        heap.sweep([](std::uint64_t, std::uint64_t) {});
        check_equal(heap.next_live(0), std::uint64_t{0}, "clean round dropped");
    }
}

NEURA_TEST(heap, published_layout_matches_the_operations) {
    // Native code reads entries at these offsets; read them here through the
    // address a view exposes and check them against the operations.
    Heap heap;
    const std::array<std::uint64_t, 3> values{1, 0xffff, 7};
    const auto array = live(heap, heap.create_collection(EntryKind::Array, 11, ElementWidth::W2, values));
    const auto* data = static_cast<const unsigned char*>(heap.elements(array).data);
    const auto* header = data - Heap::inline_data_offset();
    check_equal(static_cast<int>(header[Heap::kind_offset()]), static_cast<int>(EntryKind::Array), "kind byte");
    check_equal(static_cast<int>(header[Heap::declared_width_offset()]), 2, "declared width byte");
    check_equal(static_cast<int>(header[Heap::storage_width_offset()]), 2, "storage width byte");
    check((header[Heap::flags_offset()] & Heap::inline_flag_mask()) != 0, "inline flag");
    std::uint32_t type = 0;
    std::memcpy(&type, header + Heap::type_offset(), 4);
    check_equal(type, std::uint32_t{11}, "type word");
    std::uint64_t length = 0;
    std::memcpy(&length, header + Heap::length_offset(), 8);
    check_equal(length, std::uint64_t{3}, "length word");
    const unsigned char* pointer = nullptr;
    std::memcpy(&pointer, header + Heap::data_offset(), sizeof pointer);
    check(pointer == data, "data pointer of an inline array");
    heap.mark(array);
    check((header[Heap::flags_offset()] & Heap::marked_flag_mask()) != 0, "mark flag");
    heap.set_element(array, 0, std::uint64_t{1} << 40);
    check(header[Heap::storage_width_offset()] == 8 && (header[Heap::flags_offset()] & Heap::inline_flag_mask()) == 0,
          "a widened array is not inline");
    std::memcpy(&pointer, header + Heap::data_offset(), sizeof pointer);
    check(pointer == heap.elements(array).data, "data pointer of a widened array");

    const auto list = live(heap, heap.create_collection(EntryKind::List, 12, ElementWidth::W4, values));
    heap.append(list, 9);
    // A list's buffer is separate from its header, which the operations
    // do not expose; its elements are at the storage width.
    const auto* list_data = static_cast<const unsigned char*>(heap.elements(list).data);
    std::uint32_t third = 0;
    std::memcpy(&third, list_data + 2 * 4, 4);
    check_equal(third, std::uint32_t{7}, "a list's buffer holds W4 elements");
    check_equal(Heap::first_field_offset(0), std::size_t{24}, "fields of an object without fields");
    check_equal(Heap::first_field_offset(3), Heap::initialized_offset() + 8, "one initialized word");
    check_equal(Heap::first_field_offset(130), Heap::initialized_offset() + 24, "three initialized words");
}

namespace {

struct Walks {
    std::vector<std::uint64_t> each;
    std::vector<std::uint64_t> next;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> dropped;
    std::size_t pages = 0;
};

// The live entries by for_each() and by next_live(), in order.
void walk(const Heap& heap, Walks& walks) {
    heap.for_each([&](std::uint64_t handle, Heap::Ref) { walks.each.push_back(handle); });
    for (auto h = heap.next_live(0); h != 0; h = heap.next_live(h)) walks.next.push_back(h);
}

// Three entries that stay live, then `cycles` allocations of garbage that
// sweeps (every 100 allocations) and free() (every seventh) drop.
Walks after_cycles(std::size_t cycles) {
    Heap heap;
    const std::array<std::uint64_t, 3> keep{heap.create_object(1, 2), heap.create_text(EntryKind::Text, 2, "kept"),
                                           heap.create_filled(EntryKind::List, 3, ElementWidth::W4, 3, 7)};
    const auto mark_kept = [&](std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) heap.mark(live(heap, keep[i]));
    };
    for (std::size_t i = 0; i < cycles; ++i) {
        const auto h = heap.create_filled(EntryKind::Array, 1, ElementWidth::W1, i % 50, 1);
        if (i % 7 == 0) heap.free(h);
        if (i % 100 == 99) {
            mark_kept(3);
            heap.sweep([](std::uint64_t, std::uint64_t) {});
        }
    }
    mark_kept(3);
    heap.sweep([](std::uint64_t, std::uint64_t) {});
    Walks walks;
    walk(heap, walks);
    // Nothing left to drop but keep[2]: the pages are the first and the
    // page of the next handle.
    mark_kept(2);
    heap.sweep([&](std::uint64_t handle, std::uint64_t cells) { walks.dropped.emplace_back(handle, cells); });
    walks.pages = heap.last_sweep_pages();
    return walks;
}

} // namespace

NEURA_TEST(heap, walks_do_not_grow_with_the_handles_ever_assigned) {
    const auto few = after_cycles(1000);
    const auto many = after_cycles(100000);
    const std::vector<std::uint64_t> kept{1, 2, 3};
    check(few.each == kept && many.each == kept, "for_each visits the live entries only");
    check(few.next == kept && many.next == kept, "next_live visits the live entries only");
    check(few.dropped == many.dropped &&
              few.dropped == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{3, 4}},
          "a sweep drops the same entry with the same cells");
    check_equal(few.pages, std::size_t{2}, "pages a sweep visits after 10^3 cycles");
    check_equal(many.pages, few.pages, "pages a sweep visits after 10^5 cycles");
}

NEURA_TEST(heap, walks_skip_released_pages_between_live_ones) {
    // Ten full pages (handles 1 to 2560, pages 0 to 10); live entries are
    // left in the first, a middle, and the last page, freed one by one.
    Heap heap;
    for (int i = 0; i < 2560; ++i) heap.create_object(1, 1);
    const std::vector<std::uint64_t> kept{1, 1280, 2560};
    for (std::uint64_t h = 1; h <= 2560; ++h) {
        if (std::find(kept.begin(), kept.end(), h) == kept.end()) heap.free(h);
    }
    Walks walks;
    walk(heap, walks);
    check(walks.each == kept && walks.next == kept, "the walks find the three entries in handle order");
    check_equal(heap.next_live(300), std::uint64_t{1280}, "next_live from inside a released page");
    check_equal(heap.next_live(1279), std::uint64_t{1280}, "next_live from the handle before");
    check_equal(heap.next_live(2560), std::uint64_t{0}, "nothing after the last");
    check_equal(heap.next_live(std::numeric_limits<std::uint64_t>::max()), std::uint64_t{0}, "the largest handle");
    for (const auto h : kept) heap.mark(live(heap, h));
    std::vector<std::uint64_t> dropped;
    heap.sweep([&](std::uint64_t handle, std::uint64_t) { dropped.push_back(handle); });
    check(dropped.empty(), "nothing dropped");
    check_equal(heap.last_sweep_pages(), std::size_t{3}, "the sweep visits the three pages with entries");
    heap.mark(live(heap, 2560));
    heap.sweep([&](std::uint64_t handle, std::uint64_t) { dropped.push_back(handle); });
    check(dropped == std::vector<std::uint64_t>{1, 1280}, "a sweep drops in handle order");
    check_equal(heap.create_object(1, 0), std::uint64_t{2561}, "handles keep counting");
    walks = {};
    walk(heap, walks);
    check(walks.each == (std::vector<std::uint64_t>{2560, 2561}) && walks.next == walks.each, "walks after the sweep");
}

} // namespace neura::testing
