#pragma once

#include "nvr/image.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// Whether this translation unit is built with AddressSanitizer. The heap then
// takes every block from operator new and returns it to operator delete, so
// that the sanitizer sees each entry's bounds and lifetime, unless
// NEURA_HEAP_POOL_UNDER_SANITIZERS is defined (the CMake option of that
// name): then the size-class pool is used and poisons the blocks it holds.
#if defined(__SANITIZE_ADDRESS__)
#define NEURA_HEAP_ADDRESS_SANITIZER 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define NEURA_HEAP_ADDRESS_SANITIZER 1
#endif
#endif
#if defined(NEURA_HEAP_ADDRESS_SANITIZER)
#include <sanitizer/asan_interface.h>
#endif

// The element accessors are inlined into the executor's state operations;
// widening and list growth, which are rare, are kept out of them.
#if defined(__GNUC__) || defined(__clang__)
#define NEURA_HEAP_INLINE [[gnu::always_inline]] inline
#define NEURA_HEAP_OUT_OF_LINE [[gnu::noinline]]
#else
#define NEURA_HEAP_INLINE inline
#define NEURA_HEAP_OUT_OF_LINE
#endif

namespace neura::nvm {

// The heap of one execution (docs/runtime/NVM.md, "Heap and objects" and
// "Garbage collection"): every object, array, list, text, and bytes entry,
// named by handles that count up from one and are never reused.
//
// Callers name an entry by a Heap::Ref obtained from find(), read and write
// it through the operations below, and build new entries through create_*()
// or a Builder. How an entry is stored is published for native code only
// ("Published layout" below); C++ callers use the operations. The heap
// applies no language rule: the executor checks types, indices,
// initialization, limits, and the collection schedule before it calls an
// operation, and every operation here assumes those checks passed.
//
// Values are slot bits: a reference is its handle, a scalar its bits as a
// frame slot holds them. An element or field reads back exactly the bits
// that were stored.

enum class EntryKind : std::uint8_t { Object, Array, List, Text, Bytes };

// Bytes per element of an array or list as the heap may store it, and the
// unit of the bulk copies. Text and bytes entries hold one byte per element.
enum class ElementWidth : std::uint8_t { W1 = 1, W2 = 2, W4 = 4, W8 = 8 };

// The width an element of type `element` needs: its bit width for a scalar,
// one byte for a Boolean, eight bytes for a reference.
inline ElementWidth element_width(const nvr::Type& element) {
    switch (element.kind) {
    case nvr::TypeKind::Bool: return ElementWidth::W1;
    case nvr::TypeKind::Unsigned:
    case nvr::TypeKind::Signed:
    case nvr::TypeKind::Float:
    case nvr::TypeKind::BFloat:
        return element.width <= 8 ? ElementWidth::W1
             : element.width <= 16 ? ElementWidth::W2
             : element.width <= 32 ? ElementWidth::W4 : ElementWidth::W8;
    default: return ElementWidth::W8;
    }
}

class Heap {
    struct Entry;

public:
    // A live entry. Null means "not live": nil, a handle never assigned, or
    // one that was freed or collected. A Ref stays valid while its entry is
    // live, across allocations, list growth, widening, and collections that
    // keep the entry (the heap never moves an entry's header); it is invalid
    // once free() or a sweep removes the entry. It is a name only: it exposes
    // no storage.
    class Ref {
    public:
        Ref() = default;
        explicit operator bool() const { return entry_ != nullptr; }
        bool operator==(const Ref&) const = default;

    private:
        friend class Heap;
        explicit Ref(Entry* entry) : entry_(entry) {}
        Entry* entry_ = nullptr;
    };

    // The elements of an array or list, or the bytes of a text or bytes
    // entry, in place: `length` elements of `width` bytes each, in host
    // byte order. Read-only. Valid until the next call of a non-const
    // operation of this heap (any that can allocate, write, grow or shrink a
    // list, collect, or free); a Builder does not invalidate it. `width` is
    // the storage width. An entry's storage width may be wider than its
    // declared width (declared_width()): a store of a value with a bit above
    // the storage width moves the elements to eight-byte storage first. A
    // reader decides how to read the view by `width` only.
    struct ElementView {
        const void* data = nullptr;
        std::size_t length = 0;
        ElementWidth width = ElementWidth::W8;
    };

    class Builder;

    Heap() = default;
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    ~Heap() { release_all(); }

    // --- Lookup ---------------------------------------------------------

    // The entry a reference word names, or a null Ref. The only way to turn
    // a handle into an entry; the executor's checked lookups (`entry`,
    // `typed_entry`) wrap it with their traps.
    Ref find(std::uint64_t handle) const {
        const auto page = handle >> page_bits;
        if (page >= pages_.size() || !pages_[page]) return Ref{};
        return Ref{pages_[page]->entries[handle & page_mask]};
    }

    // The highest handle ever assigned; zero before the first allocation.
    // The executor's handle limit counts it.
    std::uint64_t last_handle() const { return last_; }

    // The lowest live handle above `handle`, or zero when there is none:
    // the live entries in handle order, for comparing two heaps entry by
    // entry.
    std::uint64_t next_live(std::uint64_t handle) const {
        const auto from = handle + 1;
        if (from == 0 || from > last_) return 0;
        const auto first = static_cast<std::size_t>(from >> page_bits);
        for (auto at = std::lower_bound(live_pages_.begin(), live_pages_.end(), first); at != live_pages_.end(); ++at) {
            const auto page = *at;
            const auto& stored = pages_[page];
            if (!stored) continue;
            for (auto index = page == first ? static_cast<std::size_t>(from & page_mask) : 0; index < page_size; ++index) {
                if (stored->entries[index]) return (static_cast<std::uint64_t>(page) << page_bits) | index;
            }
        }
        return 0;
    }

    // Calls `visit(handle, ref)` for every live entry in handle order. The
    // visitor must not change the heap.
    template <typename Visit>
    void for_each(Visit&& visit) const {
        for (const auto page : live_pages_) {
            if (!pages_[page]) continue;
            for (std::size_t index = 0; index < page_size; ++index) {
                if (auto* item = pages_[page]->entries[index]) {
                    visit((static_cast<std::uint64_t>(page) << page_bits) | index, Ref{item});
                }
            }
        }
    }

    // --- Every entry ----------------------------------------------------

    EntryKind kind(Ref ref) const { return ref.entry_->kind; }

    // The layout of an object; the type index of an array, list, text, or
    // bytes entry.
    std::uint32_t type(Ref ref) const { return ref.entry_->type; }

    // The heap cells the entry counts for in the executor's accounting
    // (`peak_heap_cells`): one for the entry, plus one per object field,
    // collection element, or text or bytes byte, whatever the storage.
    std::uint64_t cells(Ref ref) const { return 1 + ref.entry_->count; }
    static std::uint64_t object_cells(std::size_t fields) { return 1 + std::uint64_t{fields}; }
    static std::uint64_t collection_cells(std::uint64_t length) { return 1 + length; }
    static std::uint64_t text_cells(std::size_t bytes) { return 1 + std::uint64_t{bytes}; }

    // The longest array or list the heap can represent:
    // std::vector<std::uint64_t>().max_size(), whatever the storage. It
    // depends on the standard library, and a RepeatCollection longer than it
    // traps with its own detail text, so the value is kept as it is.
    static std::size_t max_elements() { return std::vector<std::uint64_t>().max_size(); }

    // True when the entry has no field and no element: nothing to trace.
    bool leaf(Ref ref) const { return text_like(ref.entry_->kind) || ref.entry_->count == 0; }

    // --- Objects --------------------------------------------------------

    std::size_t field_count(Ref ref) const { return static_cast<std::size_t>(ref.entry_->count); }

    bool initialized(Ref ref, std::uint32_t field) const {
        return ((flag_words(object(ref))[field >> 6] >> (field & 63U)) & 1U) != 0;
    }

    // The field's bits; zero, and meaningless, while it is not initialized.
    std::uint64_t field(Ref ref, std::uint32_t field) const { return field_words(object(ref))[field]; }

    // Stores a field and marks it initialized.
    void set_field(Ref ref, std::uint32_t field, std::uint64_t bits) {
        auto& item = object(ref);
        field_words(item)[field] = bits;
        flag_words(item)[field >> 6] |= std::uint64_t{1} << (field & 63U);
    }

    // Puts back a field and its initialized flag as an undo log recorded
    // them.
    void restore_field(Ref ref, std::uint32_t field, std::uint64_t bits, bool initialized) {
        auto& item = object(ref);
        field_words(item)[field] = bits;
        auto& word = flag_words(item)[field >> 6];
        const auto bit = std::uint64_t{1} << (field & 63U);
        word = initialized ? word | bit : word & ~bit;
    }

    // --- Arrays, lists, text, and bytes ---------------------------------

    // Elements of an array or list; bytes of a text or bytes entry.
    std::size_t length(Ref ref) const { return static_cast<std::size_t>(ref.entry_->count); }

    // Element `index` (below length()): an array or list element's bits, or
    // a text or bytes entry's byte zero-extended.
    NEURA_HEAP_INLINE std::uint64_t element(Ref ref, std::size_t index) const {
        const auto& item = sequence(ref);
        const auto width = item.head.storage;
        return load(item.data + index * static_cast<std::size_t>(width), width);
    }

    // Stores element `index` (below length()) of an array or list.
    NEURA_HEAP_INLINE void set_element(Ref ref, std::size_t index, std::uint64_t bits) {
        auto& item = sequence(ref);
        auto width = item.head.storage;
        if (!fits(bits, width)) [[unlikely]] {
            widen(ref);
            width = ElementWidth::W8;
        }
        store(bits, item.data + index * static_cast<std::size_t>(width), width);
    }

    // The element width the entry was created with: the width its creator
    // passed for an array or list, W1 for text and bytes, W8 for an object.
    // Storage may be wider (ElementView).
    ElementWidth declared_width(Ref ref) const { return ref.entry_->declared; }

    // The elements in place (ElementView states how long that holds).
    ElementView elements(Ref ref) const {
        const auto& item = sequence(ref);
        return {item.data, static_cast<std::size_t>(item.head.count), item.head.storage};
    }

    // The bytes of a text or bytes entry, under the same validity rule as
    // ElementView.
    std::string_view text(Ref ref) const {
        const auto& item = sequence(ref);
        return {reinterpret_cast<const char*>(item.data), static_cast<std::size_t>(item.head.count)};
    }

    // Writes every element to `out`, `width` bytes each in host byte order,
    // keeping each element's low `width` bytes: length() * width bytes. A
    // text or bytes entry is copied as its bytes and requires W1.
    void copy_out(Ref ref, void* out, ElementWidth width) const {
        const auto& item = sequence(ref);
        auto* bytes = static_cast<unsigned char*>(out);
        const auto count = static_cast<std::size_t>(item.head.count);
        const auto storage = item.head.storage;
        if (text_like(item.head.kind) || storage == width) {
            if (count != 0) std::memcpy(bytes, item.data, count * static_cast<std::size_t>(storage));
            return;
        }
        const auto w = static_cast<std::size_t>(width);
        const auto s = static_cast<std::size_t>(storage);
        for (std::size_t i = 0; i < count; ++i) store(load(item.data + i * s, storage), bytes + i * w, width);
    }

    // Stores elements 0 .. min(length(), count) - 1 of an array or list from
    // `in`, `width` bytes each in host byte order, zero-extended. Returns how
    // many it stored. A text or bytes entry is left unchanged and 0 returned.
    std::size_t copy_in(Ref ref, const void* in, std::size_t count, ElementWidth width) {
        auto& item = sequence(ref);
        if (text_like(item.head.kind)) return 0;
        const auto* bytes = static_cast<const unsigned char*>(in);
        const auto w = static_cast<std::size_t>(width);
        const auto stored = std::min(static_cast<std::size_t>(item.head.count), count);
        if (width > item.head.storage) {
            std::uint64_t any = 0;
            for (std::size_t i = 0; i < stored; ++i) any |= load(bytes + i * w, width);
            if (!fits(any, item.head.storage)) widen(ref);
        }
        const auto storage = item.head.storage;
        if (storage == width) {
            if (stored != 0) std::memcpy(item.data, bytes, stored * w);
            return stored;
        }
        const auto s = static_cast<std::size_t>(storage);
        for (std::size_t i = 0; i < stored; ++i) store(load(bytes + i * w, width), item.data + i * s, storage);
        return stored;
    }

    // --- Lists ----------------------------------------------------------

    // The structural version: incremented by one, modulo 2^64, by every
    // append() and remove() of the list. Zero for any other entry.
    std::uint64_t version(Ref ref) const {
        return ref.entry_->kind == EntryKind::List ? list(ref).version : 0;
    }

    // Adds an element at the end. The executor has already made room for
    // the cell (a safepoint) and counts it.
    void append(Ref ref, std::uint64_t bits) {
        auto& item = list(ref);
        if (!fits(bits, item.array.head.storage)) widen(ref);
        if (item.array.head.count == item.capacity) grow(item);
        const auto width = item.array.head.storage;
        store(bits, item.array.data + item.array.head.count * static_cast<std::size_t>(width), width);
        ++item.array.head.count;
        ++item.version;
    }

    // Removes element `index` (below length()); later elements move down.
    void remove(Ref ref, std::size_t index) {
        auto& item = list(ref);
        const auto w = static_cast<std::size_t>(item.array.head.storage);
        const auto after = static_cast<std::size_t>(item.array.head.count) - index - 1;
        if (after != 0) std::memmove(item.array.data + index * w, item.array.data + (index + 1) * w, after * w);
        --item.array.head.count;
        ++item.version;
    }

    // Increments the version of a list alone. For the shadow oracle's
    // self-test; no state operation calls it.
    void advance_version(Ref ref) { ++list(ref).version; }

    // --- Identity -------------------------------------------------------

    // The class identity number of an object, or zero while none has been
    // requested. Zero for any other entry.
    std::uint64_t identity(Ref ref) const {
        return ref.entry_->kind == EntryKind::Object ? object(ref).identity : 0;
    }

    // The last identity number assigned; zero before the first.
    std::uint64_t last_identity() const { return last_identity_; }

    // The identity number of an object, assigning the next one on the first
    // request.
    std::uint64_t ensure_identity(Ref ref) {
        auto& item = object(ref);
        if (item.identity == 0) item.identity = ++last_identity_;
        return item.identity;
    }

    // --- Allocation and freeing -----------------------------------------
    //
    // These store an entry under handle last_handle() + 1 and return that
    // handle. They never collect and check no limit: the executor checks the
    // handle, text, and cell limits and runs any collection first, with the
    // new entry's references rooted, and then counts cells(ref) as live.

    // An object of `fields` fields, none initialized.
    std::uint64_t create_object(std::uint32_t layout, std::size_t fields) {
        reserve_handle();
        return store_new(&new_object(layout, fields)->head);
    }

    // An array or list holding `elements`.
    std::uint64_t create_collection(EntryKind kind, std::uint32_t type, ElementWidth width,
                                    std::span<const std::uint64_t> elements) {
        std::uint64_t any = 0;
        if (width != ElementWidth::W8) {
            for (const auto bits : elements) any |= bits;
        }
        const auto storage = fits(any, width) ? width : ElementWidth::W8;
        reserve_handle();
        auto* item = new_collection(kind, type, width, storage, elements.size());
        if (storage == ElementWidth::W8) {
            if (!elements.empty()) std::memcpy(item->data, elements.data(), elements.size() * 8);
        } else {
            const auto s = static_cast<std::size_t>(storage);
            for (std::size_t i = 0; i < elements.size(); ++i) store(elements[i], item->data + i * s, storage);
        }
        return store_new(&item->head);
    }

    // An array or list of `length` copies of `fill`.
    std::uint64_t create_filled(EntryKind kind, std::uint32_t type, ElementWidth width, std::size_t length,
                                std::uint64_t fill) {
        const auto storage = length == 0 || fits(fill, width) ? width : ElementWidth::W8;
        reserve_handle();
        auto* item = new_collection(kind, type, width, storage, length);
        if (storage == ElementWidth::W1) {
            if (length != 0) std::memset(item->data, static_cast<int>(fill & 0xffU), length);
        } else {
            const auto s = static_cast<std::size_t>(storage);
            for (std::size_t i = 0; i < length; ++i) store(fill, item->data + i * s, storage);
        }
        return store_new(&item->head);
    }

    // A text or bytes entry holding `bytes`.
    std::uint64_t create_text(EntryKind kind, std::uint32_t type, std::string_view bytes) {
        reserve_handle();
        auto* item = new (blocks_.allocate(inline_bytes(bytes.size(), ElementWidth::W1))) ArrayEntry{};
        item->head = {kind, inline_flag, ElementWidth::W1, ElementWidth::W1, type, bytes.size()};
        item->data = reinterpret_cast<unsigned char*>(item + 1);
        if (!bytes.empty()) std::memcpy(item->data, bytes.data(), bytes.size());
        return store_new(&item->head);
    }

    // The entry a Builder holds. The builder is consumed.
    std::uint64_t create(Builder&& built);

    // Frees a live entry. The executor has subtracted cells(ref) and released
    // any scheduler record of the handle first.
    void free(std::uint64_t handle) {
        const auto page = handle >> page_bits;
        auto& stored = pages_[page];
        auto& item = stored->entries[handle & page_mask];
        release(std::exchange(item, nullptr));
        if (--stored->live == 0 && page < ((last_ + 1) >> page_bits)) {
            stored.reset();
            // The page's index stays in live_pages_ until it is compacted,
            // once at least half of the list is released pages.
            if (++released_pages_ * 2 > live_pages_.size()) compact_pages();
        }
    }

    // --- Collection -----------------------------------------------------

    // Marks a live entry; true when it was not marked before.
    bool mark(Ref ref) {
        if ((ref.entry_->flags & marked_flag) != 0) return false;
        ref.entry_->flags |= marked_flag;
        return true;
    }

    bool marked(Ref ref) const { return (ref.entry_->flags & marked_flag) != 0; }

    // Calls `visit(bits)` for each field of an object at a position in
    // `positions` (ascending) that is initialized: the reference fields of
    // its layout, as the executor's trace plan lists them.
    template <typename Visit>
    void visit_fields(Ref ref, std::span<const std::size_t> positions, Visit&& visit) const {
        const auto& item = object(ref);
        const auto* flags = flag_words(item);
        const auto* fields = field_words(item);
        for (const auto field : positions) {
            if (((flags[field >> 6] >> (field & 63U)) & 1U) != 0) visit(fields[field]);
        }
    }

    // Calls `visit(bits)` for each element of an array or list, in order.
    template <typename Visit>
    void visit_elements(Ref ref, Visit&& visit) const {
        const auto& item = sequence(ref);
        if (text_like(item.head.kind)) return;
        const auto count = static_cast<std::size_t>(item.head.count);
        const auto storage = item.head.storage;
        const auto s = static_cast<std::size_t>(storage);
        for (std::size_t i = 0; i < count; ++i) visit(load(item.data + i * s, storage));
    }

    // As visit_fields(), from the last position to the first.
    template <typename Visit>
    void visit_fields_reverse(Ref ref, std::span<const std::size_t> positions, Visit&& visit) const {
        const auto& item = object(ref);
        const auto* flags = flag_words(item);
        const auto* fields = field_words(item);
        for (auto at = positions.size(); at-- > 0;) {
            const auto field = positions[at];
            if (((flags[field >> 6] >> (field & 63U)) & 1U) != 0) visit(fields[field]);
        }
    }

    // As visit_elements(), from the last element to the first.
    template <typename Visit>
    void visit_elements_reverse(Ref ref, Visit&& visit) const {
        const auto& item = sequence(ref);
        if (text_like(item.head.kind)) return;
        const auto count = static_cast<std::size_t>(item.head.count);
        const auto storage = item.head.storage;
        const auto s = static_cast<std::size_t>(storage);
        for (auto i = count; i-- > 0;) visit(load(item.data + i * s, storage));
    }

    // Ends a collection: in handle order, clears the mark of every marked
    // entry and, for every unmarked one, calls `dropped(handle, cells)` and
    // frees it. `dropped` must not use the heap.
    template <typename Dropped>
    void sweep(Dropped&& dropped) {
        // Visits only the pages that exist, in ascending order, and drops
        // the released ones from live_pages_ as it goes.
        std::size_t kept = 0;
        swept_pages_ = 0;
        for (std::size_t at = 0; at < live_pages_.size(); ++at) {
            const auto page = live_pages_[at];
            auto& stored = pages_[page];
            if (!stored) continue;
            ++swept_pages_;
            for (std::size_t index = 0; index < page_size; ++index) {
                auto& item = stored->entries[index];
                if (!item) continue;
                if ((item->flags & marked_flag) != 0) {
                    item->flags &= static_cast<std::uint8_t>(~marked_flag);
                    continue;
                }
                dropped((static_cast<std::uint64_t>(page) << page_bits) | index, cells(Ref{item}));
                release(std::exchange(item, nullptr));
                --stored->live;
            }
            if (stored->live == 0 && page < ((last_ + 1) >> page_bits)) {
                stored.reset();
                continue;
            }
            live_pages_[kept++] = page;
        }
        live_pages_.resize(kept);
        released_pages_ = 0;
    }

    // The pages the last sweep() visited: those that existed. For tests of
    // the cost of a collection; it decides nothing.
    std::size_t last_sweep_pages() const { return swept_pages_; }

    // --- Comparison -----------------------------------------------------

    // Whether two entries, possibly of two heaps, hold the same thing: kind,
    // type, every field and its initialized flag (a field's bits only when
    // it is initialized), every element, every byte, the list version, and
    // the identity number. Marks, declared widths, and storage widths are
    // not compared.
    static bool same(const Heap& a, Ref x, const Heap& b, Ref y) {
        static_cast<void>(a);
        static_cast<void>(b);
        const auto& p = *x.entry_;
        const auto& q = *y.entry_;
        if (p.kind != q.kind || p.type != q.type || p.count != q.count) return false;
        const auto count = static_cast<std::size_t>(p.count);
        switch (p.kind) {
        case EntryKind::Object: {
            const auto& o = object(x);
            const auto& r = object(y);
            if (o.identity != r.identity) return false;
            const auto* of = flag_words(o);
            const auto* rf = flag_words(r);
            if (std::memcmp(of, rf, word_count(count) * 8) != 0) return false;
            const auto* ov = field_words(o);
            const auto* rv = field_words(r);
            for (std::size_t i = 0; i < count; ++i) {
                if (((of[i >> 6] >> (i & 63U)) & 1U) != 0 && ov[i] != rv[i]) return false;
            }
            return true;
        }
        case EntryKind::List:
            if (list(x).version != list(y).version) return false;
            [[fallthrough]];
        case EntryKind::Array: {
            const auto& o = sequence(x);
            const auto& r = sequence(y);
            if (o.head.storage == r.head.storage) {
                return count == 0 || std::memcmp(o.data, r.data, count * static_cast<std::size_t>(o.head.storage)) == 0;
            }
            for (std::size_t i = 0; i < count; ++i) {
                if (load(o.data + i * static_cast<std::size_t>(o.head.storage), o.head.storage) !=
                    load(r.data + i * static_cast<std::size_t>(r.head.storage), r.head.storage)) {
                    return false;
                }
            }
            return true;
        }
        case EntryKind::Text:
        case EntryKind::Bytes:
            return count == 0 || std::memcmp(sequence(x).data, sequence(y).data, count) == 0;
        }
        return false;
    }

    // --- Published layout -----------------------------------------------
    //
    // How an entry is stored, for native code that reads and writes fields
    // and elements inline. A Ref, and the handle table's slot for a live
    // handle, hold the address of the entry's header; the address does not
    // change while the entry is live. Every entry is one block that starts
    // with a 16-byte header (offsets in bytes, host byte order):
    //
    //   0   kind            uint8   EntryKind
    //   1   flags           uint8   bit 0 marked (collector only), bit 1
    //                               "data is inline"
    //   2   declared width  uint8   ElementWidth the creator passed (W1 for
    //                               text and bytes, W8 for objects)
    //   3   storage width   uint8   ElementWidth the elements are stored at
    //                               now: the declared width, or W8 once a
    //                               value did not fit
    //   4   type            uint32  layout of an object, type index otherwise
    //   8   count           uint64  fields of an object, elements of an array
    //                               or list, bytes of text or bytes
    //
    // Object: 16 identity (uint64, zero while not requested); 24 the
    //   initialized bits, bit i of word i / 64 for field i, in
    //   ceil(count / 64) uint64 words; then the fields, uint64 each, at
    //   first_field_offset(count). An object never widens.
    // Array, text, bytes: 16 data (a pointer to element 0); 24 the inline
    //   elements, count of them at the storage width. Text and bytes are
    //   always inline at W1. An array whose storage widened has flag bit 1
    //   clear and data pointing to a separate buffer of count uint64 words.
    // List: 16 data (a pointer to a separate buffer; while the capacity is
    //   zero, to a static block that holds nothing), 24 capacity (uint64, elements the buffer holds at the storage
    //   width), 32 version (uint64). Flag bit 1 is always clear. The buffer
    //   moves when the list grows or widens; the header does not.
    //
    // A reader takes the width from byte 3 and the elements from the pointer
    // at 16, for any array, list, text, or bytes entry. An element narrower
    // than eight bytes holds the low bytes of its bits; the bits above the
    // storage width are zero. A writer that stores a value with a bit above
    // the storage width must widen the collection first, as set_element()
    // does; native code calls back into the heap for that.
    static constexpr std::size_t kind_offset() { return offsetof(Entry, kind); }
    static constexpr std::size_t flags_offset() { return offsetof(Entry, flags); }
    static constexpr std::size_t declared_width_offset() { return offsetof(Entry, declared); }
    static constexpr std::size_t storage_width_offset() { return offsetof(Entry, storage); }
    static constexpr std::size_t type_offset() { return offsetof(Entry, type); }
    static constexpr std::size_t count_offset() { return offsetof(Entry, count); }
    static constexpr std::uint8_t marked_flag_mask() { return marked_flag; }
    static constexpr std::uint8_t inline_flag_mask() { return inline_flag; }
    static constexpr std::size_t identity_offset() { return offsetof(ObjectEntry, identity); }
    static constexpr std::size_t initialized_offset() { return sizeof(ObjectEntry); }
    static constexpr std::size_t first_field_offset(std::size_t fields) {
        return sizeof(ObjectEntry) + 8 * word_count(fields);
    }
    static constexpr std::size_t length_offset() { return offsetof(Entry, count); }
    static constexpr std::size_t data_offset() { return offsetof(ArrayEntry, data); }
    static constexpr std::size_t inline_data_offset() { return sizeof(ArrayEntry); }
    static constexpr std::size_t list_capacity_offset() { return offsetof(ListEntry, capacity); }
    static constexpr std::size_t list_version_offset() { return offsetof(ListEntry, version); }

private:
    static constexpr std::uint8_t marked_flag = 1;
    static constexpr std::uint8_t inline_flag = 2;

    // The header every entry starts with (Published layout).
    struct Entry {
        EntryKind kind;
        std::uint8_t flags;
        ElementWidth declared;
        ElementWidth storage;
        std::uint32_t type;
        std::uint64_t count;
    };
    // An object's header; its initialized words and fields follow.
    struct ObjectEntry {
        Entry head;
        std::uint64_t identity;
    };
    // The header of an array, text, or bytes entry; inline data follows.
    struct ArrayEntry {
        Entry head;
        alignas(8) unsigned char* data;
    };
    // The header of a list; its elements are in a separate buffer.
    struct ListEntry {
        ArrayEntry array;
        std::uint64_t capacity;
        std::uint64_t version;
    };
    static_assert(std::is_standard_layout_v<Entry> && std::is_standard_layout_v<ObjectEntry> &&
                  std::is_standard_layout_v<ArrayEntry> && std::is_standard_layout_v<ListEntry>);
    static_assert(std::is_trivially_destructible_v<ObjectEntry> && std::is_trivially_destructible_v<ListEntry>);
    static_assert(sizeof(Entry) == 16 && sizeof(ObjectEntry) == 24 && sizeof(ArrayEntry) == 24 &&
                  sizeof(ListEntry) == 40);

    static constexpr std::size_t word_count(std::size_t fields) { return (fields + 63) / 64; }

    // The entry a Ref names, as the header of its kind.
    static ObjectEntry& object(Ref ref) { return *reinterpret_cast<ObjectEntry*>(ref.entry_); }
    static ArrayEntry& sequence(Ref ref) { return *reinterpret_cast<ArrayEntry*>(ref.entry_); }
    static ListEntry& list(Ref ref) { return *reinterpret_cast<ListEntry*>(ref.entry_); }
    static std::uint64_t* flag_words(ObjectEntry& item) { return reinterpret_cast<std::uint64_t*>(&item + 1); }
    static const std::uint64_t* flag_words(const ObjectEntry& item) {
        return reinterpret_cast<const std::uint64_t*>(&item + 1);
    }
    static std::uint64_t* field_words(ObjectEntry& item) {
        return flag_words(item) + word_count(static_cast<std::size_t>(item.head.count));
    }
    static const std::uint64_t* field_words(const ObjectEntry& item) {
        return flag_words(item) + word_count(static_cast<std::size_t>(item.head.count));
    }

    static bool text_like(EntryKind kind) { return kind == EntryKind::Text || kind == EntryKind::Bytes; }

    // Whether `bits` has no bit above `width` bytes.
    NEURA_HEAP_INLINE static bool fits(std::uint64_t bits, ElementWidth width) {
        return width == ElementWidth::W8 || (bits >> (8 * static_cast<unsigned>(width))) == 0;
    }

    NEURA_HEAP_INLINE static void store(std::uint64_t bits, unsigned char* out, ElementWidth width) {
        switch (width) {
        case ElementWidth::W1: { const auto v = static_cast<std::uint8_t>(bits); std::memcpy(out, &v, 1); break; }
        case ElementWidth::W2: { const auto v = static_cast<std::uint16_t>(bits); std::memcpy(out, &v, 2); break; }
        case ElementWidth::W4: { const auto v = static_cast<std::uint32_t>(bits); std::memcpy(out, &v, 4); break; }
        case ElementWidth::W8: std::memcpy(out, &bits, 8); break;
        }
    }

    NEURA_HEAP_INLINE static std::uint64_t load(const unsigned char* in, ElementWidth width) {
        switch (width) {
        case ElementWidth::W1: return *in;
        case ElementWidth::W2: { std::uint16_t v = 0; std::memcpy(&v, in, 2); return v; }
        case ElementWidth::W4: { std::uint32_t v = 0; std::memcpy(&v, in, 4); return v; }
        case ElementWidth::W8: break;
        }
        std::uint64_t v = 0;
        std::memcpy(&v, in, 8);
        return v;
    }

    // Blocks for entries and list buffers. Blocks up to `largest` bytes come
    // from chunks of `chunk_bytes`, each carved, as it is first needed, into
    // blocks of one size class; a freed block goes on its class's free list
    // and is the next one that class hands out. Larger blocks come from
    // operator new and go back to operator delete when freed. Chunks are
    // kept until the heap is destroyed: a chunk is returned to the system
    // only then, when all of its blocks are free, so freeing never walks a
    // chunk and no block of a live entry is ever moved. Under
    // AddressSanitizer every block comes from operator new instead, unless
    // NEURA_HEAP_POOL_UNDER_SANITIZERS is defined; the pool then poisons the
    // blocks it holds.
    class Blocks {
    public:
        Blocks() = default;
        Blocks(const Blocks&) = delete;
        Blocks& operator=(const Blocks&) = delete;
        ~Blocks() {
            for (auto* chunk : chunks_) {
                unpoison(chunk, chunk_bytes);
                ::operator delete(chunk);
            }
        }

        // The bytes a block allocated for `bytes` holds.
        static std::size_t usable(std::size_t bytes) {
            if (!pooled || bytes == 0 || bytes > largest) return bytes;
            return sizes[class_of(bytes)];
        }

        // A block of at least `bytes` bytes, aligned to 8. For 0, a static
        // block that holds nothing, so that no element pointer is null.
        void* allocate(std::size_t bytes) {
            if (bytes == 0) return empty_;
            if (!pooled || bytes > largest) return ::operator new(bytes);
            const auto c = class_of(bytes);
            const std::size_t size = sizes[c];
            if (void* block = free_[c]) {
                unpoison(block, size);
                std::memcpy(&free_[c], block, sizeof(void*));
                return block;
            }
            if (static_cast<std::size_t>(end_[c] - next_[c]) < size) {
                auto* chunk = static_cast<unsigned char*>(::operator new(chunk_bytes));
                chunks_.push_back(chunk);
                poison(chunk, chunk_bytes);
                next_[c] = chunk;
                end_[c] = chunk + chunk_bytes;
            }
            void* block = next_[c];
            next_[c] += size;
            unpoison(block, size);
            return block;
        }

        // Frees a block that allocate(`bytes`) returned.
        void deallocate(void* block, std::size_t bytes) {
            if (bytes == 0) return;
            if (!pooled || bytes > largest) {
                ::operator delete(block);
                return;
            }
            const auto c = class_of(bytes);
            std::memcpy(block, &free_[c], sizeof(void*));
            free_[c] = block;
            poison(block, sizes[c]);
        }

    private:
#if defined(NEURA_HEAP_ADDRESS_SANITIZER) && !defined(NEURA_HEAP_POOL_UNDER_SANITIZERS)
        static constexpr bool pooled = false;
#else
        static constexpr bool pooled = true;
#endif
        // Size classes: every 8 bytes up to 128, every 16 up to 256, then
        // four per doubling up to 2 KiB. A class wastes at most 1/8 of a
        // block above 128 bytes; an object's header and fields are a
        // multiple of 8, so small objects waste nothing.
        static constexpr std::size_t largest = 2048;
        static constexpr std::size_t chunk_bytes = std::size_t{64} * 1024;
        static constexpr std::array<std::uint16_t, 36> sizes{
            8,   16,  24,  32,  40,  48,  56,  64,  72,   80,   88,   96,   104,  112,  120,  128,  144,  160,
            176, 192, 208, 224, 240, 256, 320, 384, 448, 512, 640, 768, 896, 1024, 1280, 1536, 1792, 2048};
        static constexpr std::array<std::uint8_t, largest / 8 + 1> classes = [] {
            std::array<std::uint8_t, largest / 8 + 1> table{};
            std::size_t c = 0;
            for (std::size_t i = 0; i < table.size(); ++i) {
                while (sizes[c] < i * 8) ++c;
                table[i] = static_cast<std::uint8_t>(c);
            }
            return table;
        }();
        static std::size_t class_of(std::size_t bytes) { return classes[(bytes + 7) / 8]; }

        static void poison(const void* block, std::size_t bytes) {
#if defined(NEURA_HEAP_ADDRESS_SANITIZER)
            ASAN_POISON_MEMORY_REGION(block, bytes);
#else
            static_cast<void>(block);
            static_cast<void>(bytes);
#endif
        }
        static void unpoison(const void* block, std::size_t bytes) {
#if defined(NEURA_HEAP_ADDRESS_SANITIZER)
            ASAN_UNPOISON_MEMORY_REGION(block, bytes);
#else
            static_cast<void>(block);
            static_cast<void>(bytes);
#endif
        }

        alignas(8) static inline unsigned char empty_[8]{};

        std::array<void*, sizes.size()> free_{};
        std::array<unsigned char*, sizes.size()> next_{};
        std::array<unsigned char*, sizes.size()> end_{};
        std::vector<unsigned char*> chunks_;
    };

    // The block of an object, or of an array, text, or bytes entry whose
    // `length` elements are inline at `width`.
    static std::size_t object_bytes(std::size_t fields) {
        return sizeof(ObjectEntry) + 8 * (word_count(fields) + fields);
    }
    static std::size_t inline_bytes(std::size_t length, ElementWidth width) {
        return sizeof(ArrayEntry) + length * static_cast<std::size_t>(width);
    }

    // A new object block with no field initialized and every field zero.
    ObjectEntry* new_object(std::uint32_t layout, std::size_t fields) {
        auto* item = new (blocks_.allocate(object_bytes(fields))) ObjectEntry{};
        item->head = {EntryKind::Object, 0, ElementWidth::W8, ElementWidth::W8, layout, fields};
        const auto words = word_count(fields) + fields;
        if (words != 0) std::memset(static_cast<void*>(item + 1), 0, words * 8);
        return item;
    }

    // A new array (inline) or list (with a buffer of at least `length`
    // elements) of `length` elements stored at `storage`, not yet written.
    ArrayEntry* new_collection(EntryKind kind, std::uint32_t type, ElementWidth declared, ElementWidth storage,
                               std::size_t length) {
        if (kind == EntryKind::List) {
            const auto w = static_cast<std::size_t>(storage);
            const auto bytes = Blocks::usable(length * w);
            auto* data = static_cast<unsigned char*>(blocks_.allocate(bytes));
            void* block = nullptr;
            try {
                block = blocks_.allocate(sizeof(ListEntry));
            } catch (...) {
                blocks_.deallocate(data, bytes);
                throw;
            }
            auto* item = new (block) ListEntry{};
            item->array.head = {kind, 0, declared, storage, type, length};
            item->array.data = data;
            item->capacity = bytes / w;
            return &item->array;
        }
        auto* item = new (blocks_.allocate(inline_bytes(length, storage))) ArrayEntry{};
        item->head = {kind, inline_flag, declared, storage, type, length};
        item->data = reinterpret_cast<unsigned char*>(item + 1);
        return item;
    }

    // Moves the elements of an array or list to eight-byte storage. The
    // header stays; a list keeps its capacity, an array's inline elements
    // are left unused until it is freed.
    NEURA_HEAP_OUT_OF_LINE void widen(Ref ref) {
        auto& item = sequence(ref);
        const auto from = item.head.storage;
        const auto length = static_cast<std::size_t>(item.head.count);
        const bool is_list = item.head.kind == EntryKind::List;
        const auto capacity = is_list ? static_cast<std::size_t>(list(ref).capacity) : length;
        auto* data = static_cast<unsigned char*>(blocks_.allocate(capacity * 8));
        const auto s = static_cast<std::size_t>(from);
        for (std::size_t i = 0; i < length; ++i) store(load(item.data + i * s, from), data + i * 8, ElementWidth::W8);
        if (is_list) blocks_.deallocate(item.data, capacity * s);
        item.head.flags &= static_cast<std::uint8_t>(~inline_flag);
        item.data = data;
        item.head.storage = ElementWidth::W8;
    }

    // Gives a full list room for at least one more element: at least twice
    // the capacity, and at least four elements.
    NEURA_HEAP_OUT_OF_LINE void grow(ListEntry& item) {
        const auto w = static_cast<std::size_t>(item.array.head.storage);
        const auto capacity = static_cast<std::size_t>(item.capacity);
        const auto bytes = Blocks::usable(std::max<std::size_t>(capacity * 2, 4) * w);
        auto* data = static_cast<unsigned char*>(blocks_.allocate(bytes));
        const auto length = static_cast<std::size_t>(item.array.head.count);
        if (length != 0) std::memcpy(data, item.array.data, length * w);
        blocks_.deallocate(item.array.data, capacity * w);
        item.array.data = data;
        item.capacity = bytes / w;
    }

    // Makes sure the page of handle last_handle() + 1 exists, before the
    // entry's blocks are allocated, so that store_new() cannot fail.
    void reserve_handle() {
        const auto page = (last_ + 1) >> page_bits;
        if (page >= pages_.size()) pages_.resize(page + 1);
        auto& stored = pages_[page];
        if (!stored) {
            live_pages_.push_back(static_cast<std::size_t>(page));
            try {
                stored = std::make_unique<Page>();
            } catch (...) {
                live_pages_.pop_back();
                throw;
            }
        }
    }

    // Removes the indices of released pages from live_pages_.
    void compact_pages() {
        std::erase_if(live_pages_, [this](std::size_t page) { return !pages_[page]; });
        released_pages_ = 0;
    }

    // Stores an entry under handle last_handle() + 1, whose page
    // reserve_handle() made.
    std::uint64_t store_new(Entry* item) {
        const auto handle = last_ + 1;
        auto& stored = *pages_[handle >> page_bits];
        stored.entries[handle & page_mask] = item;
        ++stored.live;
        last_ = handle;
        return handle;
    }

    // Frees the entry's block and any buffer it owns.
    void release(Entry* item) {
        switch (item->kind) {
        case EntryKind::Object:
            blocks_.deallocate(item, object_bytes(static_cast<std::size_t>(item->count)));
            return;
        case EntryKind::List: {
            auto* l = reinterpret_cast<ListEntry*>(item);
            blocks_.deallocate(l->array.data,
                               static_cast<std::size_t>(l->capacity) * static_cast<std::size_t>(item->storage));
            blocks_.deallocate(item, sizeof(ListEntry));
            return;
        }
        case EntryKind::Array:
            if ((item->flags & inline_flag) == 0) {
                const auto length = static_cast<std::size_t>(item->count);
                blocks_.deallocate(reinterpret_cast<ArrayEntry*>(item)->data, length * 8);
                blocks_.deallocate(item, inline_bytes(length, item->declared));
                return;
            }
            break;
        case EntryKind::Text:
        case EntryKind::Bytes:
            break;
        }
        blocks_.deallocate(item, inline_bytes(static_cast<std::size_t>(item->count), item->storage));
    }

    void release_all() {
        for (auto& stored : pages_) {
            if (!stored) continue;
            for (auto* item : stored->entries) {
                if (item) release(item);
            }
        }
    }

    // Handles index a two-level table of pages directly: a page is allocated
    // when its first handle is assigned and released once every handle it
    // can hold has been assigned and freed. An entry's header keeps its
    // address while the entry is live.
    static constexpr unsigned page_bits = 8;
    static constexpr std::size_t page_size = std::size_t{1} << page_bits;
    static constexpr std::uint64_t page_mask = page_size - 1;

    struct Page {
        std::array<Entry*, page_size> entries{};
        std::size_t live = 0;
    };

    Blocks blocks_;
    std::vector<std::unique_ptr<Page>> pages_;
    // The indices of the pages that exist, ascending, so that the walks in
    // handle order (sweep, for_each, next_live) skip released pages. A page
    // is appended when its first handle is reserved, which is above every
    // page before it. The index of a page that free() releases stays until
    // a sweep or compact_pages() removes it; released_pages_ counts those.
    std::vector<std::size_t> live_pages_;
    std::size_t released_pages_ = 0;
    std::size_t swept_pages_ = 0;
    std::uint64_t last_ = 0;
    std::uint64_t last_identity_ = 0;
};

static_assert(Heap::kind_offset() == 0 && Heap::flags_offset() == 1 && Heap::declared_width_offset() == 2 &&
              Heap::storage_width_offset() == 3 && Heap::type_offset() == 4 && Heap::count_offset() == 8);
static_assert(Heap::marked_flag_mask() == 1 && Heap::inline_flag_mask() == 2);
static_assert(Heap::identity_offset() == 16 && Heap::initialized_offset() == 24 && Heap::first_field_offset(0) == 24 &&
              Heap::first_field_offset(1) == 32 && Heap::first_field_offset(64) == 32 &&
              Heap::first_field_offset(65) == 40);
static_assert(Heap::length_offset() == 8 && Heap::data_offset() == 16 && Heap::inline_data_offset() == 24 &&
              Heap::list_capacity_offset() == 24 && Heap::list_version_offset() == 32);

// An entry being built outside the heap: the result of an operation that
// computes its contents before it allocates (text building, host values,
// imported graphs, kernel results, launch arguments). It is not live and has
// no handle; Heap::create() copies it into a new entry. While it is built, a
// collection can run; the executor roots the references the builder already
// holds (through references() or visit_references()). Building never touches
// the heap.
class Heap::Builder {
public:
    // An object of `fields` fields, none initialized.
    static Builder object(std::uint32_t layout, std::size_t fields) {
        Builder b;
        b.kind_ = EntryKind::Object;
        b.type_ = layout;
        b.cells_.assign(fields, 0);
        b.initialized_.assign(fields, 0);
        return b;
    }

    // An empty array or list.
    static Builder collection(EntryKind kind, std::uint32_t type, ElementWidth width) {
        Builder b;
        b.kind_ = kind;
        b.type_ = type;
        b.width_ = width;
        return b;
    }

    // An empty text or bytes entry.
    static Builder text(EntryKind kind, std::uint32_t type) {
        Builder b;
        b.kind_ = kind;
        b.type_ = type;
        b.width_ = ElementWidth::W1;
        return b;
    }

    EntryKind kind() const { return kind_; }
    std::uint32_t type() const { return type_; }

    // The cells the entry will count for (Heap::cells()).
    std::uint64_t cells() const { return 1 + cells_.size() + bytes_.size(); }

    // Objects: stores a field and marks it initialized.
    void set_field(std::uint32_t field, std::uint64_t bits) {
        cells_[field] = bits;
        initialized_[field] = 1;
    }

    // Arrays and lists.
    std::size_t length() const { return cells_.size(); }
    void reserve(std::size_t length) { cells_.reserve(length); }
    void push(std::uint64_t bits) { cells_.push_back(bits); }
    // One element per byte of `bytes`, zero-extended.
    void push_bytes(const unsigned char* bytes, std::size_t count) { cells_.insert(cells_.end(), bytes, bytes + count); }
    // The elements so far of a collection whose elements are references, as
    // temporary roots for a collection that runs while it is built. Valid
    // until the builder changes.
    std::span<const std::uint64_t> references() const { return cells_; }

    // Text and bytes.
    std::size_t size() const { return bytes_.size(); }
    void reserve_bytes(std::size_t bytes) { bytes_.reserve(bytes); }
    void append(std::string_view bytes) { bytes_.append(bytes); }
    void push_byte(unsigned char byte) { bytes_.push_back(static_cast<char>(byte)); }
    // The low byte of every element of array or list `source` of `heap`.
    void append_elements(const Heap& heap, Ref source) {
        const auto count = heap.length(source);
        bytes_.reserve(bytes_.size() + count);
        for (std::size_t i = 0; i < count; ++i) {
            bytes_.push_back(static_cast<char>(static_cast<unsigned char>(heap.element(source, i))));
        }
    }
    // Empties the bytes and releases their storage.
    void clear_bytes() { std::string{}.swap(bytes_); }
    // The bytes so far, valid until the builder changes.
    std::string_view bytes() const { return bytes_; }

    // As Heap::visit_fields() and Heap::visit_elements(), for the entry
    // being built: what a collection marks through the allocation in flight.
    template <typename Visit>
    void visit_fields(std::span<const std::size_t> positions, Visit&& visit) const {
        for (const auto field : positions) {
            if (initialized_[field]) visit(cells_[field]);
        }
    }
    template <typename Visit>
    void visit_elements(Visit&& visit) const {
        for (const auto bits : cells_) visit(bits);
    }
    // As visit_fields() and visit_elements(), last to first.
    template <typename Visit>
    void visit_fields_reverse(std::span<const std::size_t> positions, Visit&& visit) const {
        for (auto at = positions.size(); at-- > 0;) {
            const auto field = positions[at];
            if (initialized_[field]) visit(cells_[field]);
        }
    }
    template <typename Visit>
    void visit_elements_reverse(Visit&& visit) const {
        for (auto at = cells_.size(); at-- > 0;) visit(cells_[at]);
    }

private:
    friend class Heap;
    EntryKind kind_ = EntryKind::Object;
    // The declared element width (Heap::declared_width()).
    ElementWidth width_ = ElementWidth::W8;
    std::uint32_t type_ = 0;
    std::vector<std::uint64_t> cells_;
    std::vector<std::uint8_t> initialized_;
    std::string bytes_;
};

inline std::uint64_t Heap::create(Builder&& built) {
    switch (built.kind_) {
    case EntryKind::Object: {
        reserve_handle();
        auto* item = new_object(built.type_, built.cells_.size());
        auto* flags = flag_words(*item);
        auto* fields = field_words(*item);
        for (std::size_t i = 0; i < built.cells_.size(); ++i) {
            fields[i] = built.cells_[i];
            if (built.initialized_[i] != 0) flags[i >> 6] |= std::uint64_t{1} << (i & 63U);
        }
        return store_new(&item->head);
    }
    case EntryKind::Array:
    case EntryKind::List:
        return create_collection(built.kind_, built.type_, built.width_, built.cells_);
    case EntryKind::Text:
    case EntryKind::Bytes:
        break;
    }
    return create_text(built.kind_, built.type_, built.bytes_);
}

} // namespace neura::nvm
