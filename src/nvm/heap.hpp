#pragma once

#include "nvr/image.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neura::nvm {

// The heap of one execution (docs/runtime/NVM.md, "Heap and objects" and
// "Garbage collection"): every object, array, list, text, and bytes entry,
// named by handles that count up from one and are never reused.
//
// Nothing outside this class knows how an entry is stored. Callers name an
// entry by a Heap::Ref obtained from find(), read and write it through the
// operations below, and build new entries through create_*() or a Builder.
// The heap applies no language rule: the executor checks types, indices,
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
    // live, across allocations, list growth, and collections that keep the
    // entry (the heap never moves an entry); it is invalid once free() or a
    // sweep removes the entry. It is a name only: it exposes no storage.
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
    // declared width (declared_width()); a reader decides how to read the
    // view by `width` only.
    struct ElementView {
        const void* data = nullptr;
        std::size_t length = 0;
        ElementWidth width = ElementWidth::W8;
    };

    class Builder;

    Heap() = default;
    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;

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
        for (auto h = handle + 1; h != 0 && h <= last_; ++h) {
            const auto page = h >> page_bits;
            if (!pages_[page]) {
                h |= page_mask;
                continue;
            }
            if (pages_[page]->entries[h & page_mask]) return h;
        }
        return 0;
    }

    // Calls `visit(handle, ref)` for every live entry in handle order. The
    // visitor must not change the heap.
    template <typename Visit>
    void for_each(Visit&& visit) const {
        for (std::size_t page = 0; page < pages_.size(); ++page) {
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
    std::uint64_t cells(Ref ref) const {
        return 1 + ref.entry_->cells.size() + ref.entry_->bytes.size();
    }
    static std::uint64_t object_cells(std::size_t fields) { return 1 + std::uint64_t{fields}; }
    static std::uint64_t collection_cells(std::uint64_t length) { return 1 + length; }
    static std::uint64_t text_cells(std::size_t bytes) { return 1 + std::uint64_t{bytes}; }

    // The longest array or list the heap can represent:
    // std::vector<std::uint64_t>().max_size(), whatever the storage. It
    // depends on the standard library, and a RepeatCollection longer than it
    // traps with its own detail text, so the value is kept as it is.
    static std::size_t max_elements() { return std::vector<std::uint64_t>().max_size(); }

    // True when the entry has no field and no element: nothing to trace.
    bool leaf(Ref ref) const { return ref.entry_->cells.empty(); }

    // --- Objects --------------------------------------------------------

    std::size_t field_count(Ref ref) const { return ref.entry_->cells.size(); }

    bool initialized(Ref ref, std::uint32_t field) const { return ref.entry_->initialized[field] != 0; }

    // The field's bits; zero, and meaningless, while it is not initialized.
    std::uint64_t field(Ref ref, std::uint32_t field) const { return ref.entry_->cells[field]; }

    // Stores a field and marks it initialized.
    void set_field(Ref ref, std::uint32_t field, std::uint64_t bits) {
        ref.entry_->cells[field] = bits;
        ref.entry_->initialized[field] = 1;
    }

    // Puts back a field and its initialized flag as an undo log recorded
    // them.
    void restore_field(Ref ref, std::uint32_t field, std::uint64_t bits, bool initialized) {
        ref.entry_->cells[field] = bits;
        ref.entry_->initialized[field] = initialized ? 1 : 0;
    }

    // --- Arrays, lists, text, and bytes ---------------------------------

    // Elements of an array or list; bytes of a text or bytes entry.
    std::size_t length(Ref ref) const {
        const auto& item = *ref.entry_;
        return text_like(item.kind) ? item.bytes.size() : item.cells.size();
    }

    // Element `index` (below length()): an array or list element's bits, or
    // a text or bytes entry's byte zero-extended.
    std::uint64_t element(Ref ref, std::size_t index) const {
        const auto& item = *ref.entry_;
        if (text_like(item.kind)) return static_cast<unsigned char>(item.bytes[index]);
        return item.cells[index];
    }

    // Stores element `index` (below length()) of an array or list.
    void set_element(Ref ref, std::size_t index, std::uint64_t bits) { ref.entry_->cells[index] = bits; }

    // The element width the entry was created with: the width its creator
    // passed for an array or list, W1 for text and bytes, W8 for an object.
    // Storage may be wider (ElementView).
    ElementWidth declared_width(Ref ref) const { return ref.entry_->width; }

    // The elements in place (ElementView states how long that holds).
    ElementView elements(Ref ref) const {
        const auto& item = *ref.entry_;
        if (text_like(item.kind)) return {item.bytes.data(), item.bytes.size(), ElementWidth::W1};
        return {item.cells.data(), item.cells.size(), ElementWidth::W8};
    }

    // The bytes of a text or bytes entry, under the same validity rule as
    // ElementView.
    std::string_view text(Ref ref) const { return ref.entry_->bytes; }

    // Writes every element to `out`, `width` bytes each in host byte order,
    // keeping each element's low `width` bytes: length() * width bytes. A
    // text or bytes entry is copied as its bytes and requires W1.
    void copy_out(Ref ref, void* out, ElementWidth width) const {
        const auto& item = *ref.entry_;
        auto* bytes = static_cast<unsigned char*>(out);
        if (text_like(item.kind)) {
            if (!item.bytes.empty()) std::memcpy(bytes, item.bytes.data(), item.bytes.size());
            return;
        }
        const auto w = static_cast<std::size_t>(width);
        for (std::size_t i = 0; i < item.cells.size(); ++i) store(item.cells[i], bytes + i * w, width);
    }

    // Stores elements 0 .. min(length(), count) - 1 of an array or list from
    // `in`, `width` bytes each in host byte order, zero-extended. Returns how
    // many it stored. A text or bytes entry is left unchanged and 0 returned.
    std::size_t copy_in(Ref ref, const void* in, std::size_t count, ElementWidth width) {
        auto& item = *ref.entry_;
        if (text_like(item.kind)) return 0;
        const auto* bytes = static_cast<const unsigned char*>(in);
        const auto w = static_cast<std::size_t>(width);
        const auto stored = std::min(item.cells.size(), count);
        for (std::size_t i = 0; i < stored; ++i) item.cells[i] = load(bytes + i * w, width);
        return stored;
    }

    // --- Lists ----------------------------------------------------------

    // The structural version: incremented by one, modulo 2^64, by every
    // append() and remove() of the list.
    std::uint64_t version(Ref ref) const { return ref.entry_->version; }

    // Adds an element at the end. The executor has already made room for
    // the cell (a safepoint) and counts it.
    void append(Ref ref, std::uint64_t bits) {
        ref.entry_->cells.push_back(bits);
        ++ref.entry_->version;
    }

    // Removes element `index` (below length()); later elements move down.
    void remove(Ref ref, std::size_t index) {
        auto& cells = ref.entry_->cells;
        cells.erase(cells.begin() + static_cast<std::ptrdiff_t>(index));
        ++ref.entry_->version;
    }

    // Increments the version alone. For the shadow oracle's self-test; no
    // state operation calls it.
    void advance_version(Ref ref) { ++ref.entry_->version; }

    // --- Identity -------------------------------------------------------

    // The class identity number of an object, or zero while none has been
    // requested.
    std::uint64_t identity(Ref ref) const { return ref.entry_->identity; }

    // The last identity number assigned; zero before the first.
    std::uint64_t last_identity() const { return last_identity_; }

    // The identity number, assigning the next one on the first request.
    std::uint64_t ensure_identity(Ref ref) {
        auto& item = *ref.entry_;
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
        Entry item;
        item.kind = EntryKind::Object;
        item.type = layout;
        item.cells.assign(fields, 0);
        item.initialized.assign(fields, 0);
        return store_new(std::move(item));
    }

    // An array or list holding `elements`.
    std::uint64_t create_collection(EntryKind kind, std::uint32_t type, ElementWidth width,
                                    std::span<const std::uint64_t> elements) {
        Entry item;
        item.kind = kind;
        item.type = type;
        item.width = width;
        item.cells.assign(elements.begin(), elements.end());
        return store_new(std::move(item));
    }

    // An array or list of `length` copies of `fill`.
    std::uint64_t create_filled(EntryKind kind, std::uint32_t type, ElementWidth width, std::size_t length,
                                std::uint64_t fill) {
        Entry item;
        item.kind = kind;
        item.type = type;
        item.width = width;
        item.cells.assign(length, fill);
        return store_new(std::move(item));
    }

    // A text or bytes entry holding `bytes`.
    std::uint64_t create_text(EntryKind kind, std::uint32_t type, std::string_view bytes) {
        Entry item;
        item.kind = kind;
        item.type = type;
        item.width = ElementWidth::W1;
        item.bytes.assign(bytes);
        return store_new(std::move(item));
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
        if (--stored->live == 0 && page < ((last_ + 1) >> page_bits)) stored.reset();
    }

    // --- Collection -----------------------------------------------------

    // Marks a live entry; true when it was not marked before.
    bool mark(Ref ref) {
        if (ref.entry_->marked) return false;
        ref.entry_->marked = true;
        return true;
    }

    bool marked(Ref ref) const { return ref.entry_->marked; }

    // Calls `visit(bits)` for each field of an object at a position in
    // `positions` (ascending) that is initialized: the reference fields of
    // its layout, as the executor's trace plan lists them.
    template <typename Visit>
    void visit_fields(Ref ref, std::span<const std::size_t> positions, Visit&& visit) const {
        const auto& item = *ref.entry_;
        for (const auto field : positions) {
            if (item.initialized[field]) visit(item.cells[field]);
        }
    }

    // Calls `visit(bits)` for each element of an array or list, in order.
    template <typename Visit>
    void visit_elements(Ref ref, Visit&& visit) const {
        for (const auto bits : ref.entry_->cells) visit(bits);
    }

    // Ends a collection: in handle order, clears the mark of every marked
    // entry and, for every unmarked one, calls `dropped(handle, cells)` and
    // frees it. `dropped` must not use the heap.
    template <typename Dropped>
    void sweep(Dropped&& dropped) {
        for (std::size_t page = 0; page < pages_.size(); ++page) {
            auto& stored = pages_[page];
            if (!stored) continue;
            for (std::size_t index = 0; index < page_size; ++index) {
                auto& item = stored->entries[index];
                if (!item) continue;
                if (item->marked) {
                    item->marked = false;
                    continue;
                }
                dropped((static_cast<std::uint64_t>(page) << page_bits) | index, cells(Ref{item}));
                release(std::exchange(item, nullptr));
                --stored->live;
            }
            if (stored->live == 0 && page < ((last_ + 1) >> page_bits)) stored.reset();
        }
    }

    // --- Comparison -----------------------------------------------------

    // Whether two entries, possibly of two heaps, hold the same thing: kind,
    // type, every field and its initialized flag (a field's bits only when
    // it is initialized), every element, every byte, the list version, and
    // the identity number. Marks are not compared.
    static bool same(const Heap& a, Ref x, const Heap& b, Ref y) {
        const auto& p = *x.entry_;
        const auto& q = *y.entry_;
        static_cast<void>(a);
        static_cast<void>(b);
        if (p.kind != q.kind || p.type != q.type || p.version != q.version || p.identity != q.identity ||
            p.bytes != q.bytes || p.cells.size() != q.cells.size()) {
            return false;
        }
        if (p.kind != EntryKind::Object) return p.cells == q.cells;
        for (std::size_t i = 0; i < p.cells.size(); ++i) {
            if (p.initialized[i] != q.initialized[i] || (p.initialized[i] && p.cells[i] != q.cells[i])) return false;
        }
        return true;
    }

private:
    struct Entry {
        EntryKind kind = EntryKind::Object;
        bool marked = false;
        // The declared element width (declared_width()).
        ElementWidth width = ElementWidth::W8;
        std::uint32_t type = 0;
        std::vector<std::uint64_t> cells;
        std::vector<std::uint8_t> initialized;
        std::string bytes;
        std::uint64_t version = 0;
        std::uint64_t identity = 0;
    };

    static bool text_like(EntryKind kind) { return kind == EntryKind::Text || kind == EntryKind::Bytes; }

    static void store(std::uint64_t bits, unsigned char* out, ElementWidth width) {
        switch (width) {
        case ElementWidth::W1: { const auto v = static_cast<std::uint8_t>(bits); std::memcpy(out, &v, 1); break; }
        case ElementWidth::W2: { const auto v = static_cast<std::uint16_t>(bits); std::memcpy(out, &v, 2); break; }
        case ElementWidth::W4: { const auto v = static_cast<std::uint32_t>(bits); std::memcpy(out, &v, 4); break; }
        case ElementWidth::W8: std::memcpy(out, &bits, 8); break;
        }
    }

    static std::uint64_t load(const unsigned char* in, ElementWidth width) {
        switch (width) {
        case ElementWidth::W1: { std::uint8_t v = 0; std::memcpy(&v, in, 1); return v; }
        case ElementWidth::W2: { std::uint16_t v = 0; std::memcpy(&v, in, 2); return v; }
        case ElementWidth::W4: { std::uint32_t v = 0; std::memcpy(&v, in, 4); return v; }
        case ElementWidth::W8: break;
        }
        std::uint64_t v = 0;
        std::memcpy(&v, in, 8);
        return v;
    }

    std::uint64_t store_new(Entry&& entry) {
        const auto handle = last_ + 1;
        const auto page = handle >> page_bits;
        if (page >= pages_.size()) pages_.resize(page + 1);
        auto& stored = pages_[page];
        if (!stored) stored = std::make_unique<Page>();
        auto* item = acquire();
        *item = std::move(entry);
        stored->entries[handle & page_mask] = item;
        ++stored->live;
        last_ = handle;
        return handle;
    }

    // Handles index a two-level table of pages directly: a page is allocated
    // when its first handle is assigned and released once every handle it
    // can hold has been assigned and freed. Entries live in a pool and keep
    // their address while live.
    static constexpr unsigned page_bits = 8;
    static constexpr std::size_t page_size = std::size_t{1} << page_bits;
    static constexpr std::uint64_t page_mask = page_size - 1;
    static constexpr std::size_t chunk_size = 256;

    struct Page {
        std::array<Entry*, page_size> entries{};
        std::size_t live = 0;
    };

    Entry* acquire() {
        if (free_.empty()) {
            chunks_.push_back(std::make_unique<Entry[]>(chunk_size));
            auto* chunk = chunks_.back().get();
            for (auto index = chunk_size; index-- > 0;) free_.push_back(chunk + index);
        }
        auto* item = free_.back();
        free_.pop_back();
        return item;
    }

    // Frees the entry's storage and returns it to the pool.
    void release(Entry* item) {
        std::destroy_at(item);
        std::construct_at(item);
        free_.push_back(item);
    }

    std::vector<std::unique_ptr<Page>> pages_;
    std::vector<std::unique_ptr<Entry[]>> chunks_;
    std::vector<Entry*> free_;
    std::uint64_t last_ = 0;
    std::uint64_t last_identity_ = 0;
};

// An entry being built outside the heap: the result of an operation that
// computes its contents before it allocates (text building, host values,
// imported graphs, kernel results, launch arguments). It is not live and has
// no handle; Heap::create() stores it. While it is built, a collection can
// run; the executor roots the references the builder already holds (through
// references() or visit_references()). Building never touches the heap.
class Heap::Builder {
public:
    // An object of `fields` fields, none initialized.
    static Builder object(std::uint32_t layout, std::size_t fields) {
        Builder b;
        b.entry_.kind = EntryKind::Object;
        b.entry_.type = layout;
        b.entry_.cells.assign(fields, 0);
        b.entry_.initialized.assign(fields, 0);
        return b;
    }

    // An empty array or list.
    static Builder collection(EntryKind kind, std::uint32_t type, ElementWidth width) {
        Builder b;
        b.entry_.kind = kind;
        b.entry_.type = type;
        b.entry_.width = width;
        return b;
    }

    // An empty text or bytes entry.
    static Builder text(EntryKind kind, std::uint32_t type) {
        Builder b;
        b.entry_.kind = kind;
        b.entry_.type = type;
        b.entry_.width = ElementWidth::W1;
        return b;
    }

    EntryKind kind() const { return entry_.kind; }
    std::uint32_t type() const { return entry_.type; }

    // The cells the entry will count for (Heap::cells()).
    std::uint64_t cells() const { return 1 + entry_.cells.size() + entry_.bytes.size(); }

    // Objects: stores a field and marks it initialized.
    void set_field(std::uint32_t field, std::uint64_t bits) {
        entry_.cells[field] = bits;
        entry_.initialized[field] = 1;
    }

    // Arrays and lists.
    std::size_t length() const { return entry_.cells.size(); }
    void reserve(std::size_t length) { entry_.cells.reserve(length); }
    void push(std::uint64_t bits) { entry_.cells.push_back(bits); }
    // One element per byte of `bytes`, zero-extended.
    void push_bytes(const unsigned char* bytes, std::size_t count) { entry_.cells.insert(entry_.cells.end(), bytes, bytes + count); }
    // The elements so far of a collection whose elements are references, as
    // temporary roots for a collection that runs while it is built. Valid
    // until the builder changes.
    std::span<const std::uint64_t> references() const { return entry_.cells; }

    // Text and bytes.
    std::size_t size() const { return entry_.bytes.size(); }
    void reserve_bytes(std::size_t bytes) { entry_.bytes.reserve(bytes); }
    void append(std::string_view bytes) { entry_.bytes.append(bytes); }
    void push_byte(unsigned char byte) { entry_.bytes.push_back(static_cast<char>(byte)); }
    // The low byte of every element of array or list `source` of `heap`.
    void append_elements(const Heap& heap, Ref source) {
        const auto count = heap.length(source);
        entry_.bytes.reserve(entry_.bytes.size() + count);
        for (std::size_t i = 0; i < count; ++i) {
            entry_.bytes.push_back(static_cast<char>(static_cast<unsigned char>(heap.element(source, i))));
        }
    }
    // Empties the bytes and releases their storage.
    void clear_bytes() { std::string{}.swap(entry_.bytes); }
    // The bytes so far, valid until the builder changes.
    std::string_view bytes() const { return entry_.bytes; }

    // As Heap::visit_fields() and Heap::visit_elements(), for the entry
    // being built: what a collection marks through the allocation in flight.
    template <typename Visit>
    void visit_fields(std::span<const std::size_t> positions, Visit&& visit) const {
        for (const auto field : positions) {
            if (entry_.initialized[field]) visit(entry_.cells[field]);
        }
    }
    template <typename Visit>
    void visit_elements(Visit&& visit) const {
        for (const auto bits : entry_.cells) visit(bits);
    }

private:
    friend class Heap;
    Heap::Entry entry_;
};

inline std::uint64_t Heap::create(Builder&& built) { return store_new(std::move(built.entry_)); }

} // namespace neura::nvm
