#include "reference_storage.hpp"

#include <iterator>
#include <map>
#include <string_view>
#include <utility>

namespace neura {
namespace {

const IrType void_type{TypeKind::Void, "void", no_symbol, {}};
constexpr std::string_view parameter_preparation = "$reference_parameter";
constexpr std::string_view field_preparation = "$reference_field";
constexpr std::string_view global_preparation = "$reference_global";
constexpr std::string_view global_value = "$reference_global_value";

IrNode operation(IrOp op, const Span& span, const IrType& type = void_type, SymbolId symbol = no_symbol) {
    return {op, type, span, symbol, {}, {}, {}, {}, {}};
}

IrNode initialize(SymbolId symbol, IrNode value, std::string_view marker = {}) {
    auto result = operation(IrOp::Initialize, value.span, void_type, symbol);
    result.text = marker;
    result.operands.push_back(std::move(value));
    return result;
}

bool literal_primitive(const IrNode& node) {
    return node.op == IrOp::Integer || node.op == IrOp::Float ||
        node.op == IrOp::Boolean || node.op == IrOp::String;
}

struct Box {
    IrType type;
    SymbolId value = no_symbol;
    SymbolId empty = no_symbol;
    SymbolId initialized = no_symbol;
};

struct Storage {
    const Box* box;
    SymbolId slot;
};

class Materialization {
public:
    explicit Materialization(IrProgram& program) : program_(program) {}
    void run();

private:
    IrProgram& program_;
    std::map<SymbolId, Box> boxes_;
    std::map<SymbolId, Storage> storage_;
    std::map<SymbolId, std::vector<IrNode>> preparations_;

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols[id]; }
    void collect(const IrNode& node);
    void include_global_overrides();
    void plan();
    IrNode allocate(const Box& box, const Span& span, std::vector<IrNode> arguments = {}) const;
    IrNode value_field(IrNode box) const;
    IrNode storage_load(SymbolId referent, const Span& span) const;
    SymbolId temporary(SymbolId owner, std::string name, const IrNode& initializer);
    void rewrite(IrNode& node, SymbolId owner);
    void address(IrNode& place, const Storage& storage, SymbolId owner);
    void select_box(IrNode& place, const Storage& storage) const;
    void rewrite_initializer(IrNode& node, const Storage& storage, SymbolId owner);
    void preserve_field_store_order(IrNode& node, SymbolId owner);
};

void Materialization::collect(const IrNode& node) {
    if (node.op == IrOp::ReferencePlace) {
        const auto found = boxes_.find(node.type.symbol);
        if (found == boxes_.end() || found->second.value == no_symbol ||
            found->second.empty == no_symbol || found->second.initialized == no_symbol) {
            throw Diagnostic(node.span, "invalid primitive reference layout").with_code("E0899");
        }
        storage_.try_emplace(node.symbol, Storage{&found->second, node.symbol});
    }
    for (const auto* region : {&node.operands, &node.body, &node.alternative})
        for (const auto& child : *region) collect(child);
}

void Materialization::include_global_overrides() {
    std::map<SymbolId, std::vector<SymbolId>> related;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& entry = symbol(id);
        if (entry.kind == SymbolKind::Global && entry.overrides != no_symbol &&
            symbol(entry.overrides).kind == SymbolKind::Global) {
            related[id].push_back(entry.overrides);
            related[entry.overrides].push_back(id);
        }
    }
    std::vector<SymbolId> pending;
    for (const auto& [referent, storage] : storage_)
        if (related.contains(referent)) pending.push_back(referent);
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        for (const auto id : related.at(current)) {
            if (storage_.try_emplace(id, Storage{storage_.at(current).box, id}).second)
                pending.push_back(id);
        }
    }
}

IrNode Materialization::allocate(const Box& box, const Span& span, std::vector<IrNode> arguments) const {
    auto result = operation(IrOp::Allocate, span, box.type, arguments.empty() ? box.empty : box.initialized);
    result.operands = std::move(arguments);
    return result;
}

IrNode Materialization::value_field(IrNode box) const {
    const auto value = boxes_.at(box.type.symbol).value;
    auto result = operation(IrOp::Field, box.span, symbol(value).type, value);
    result.operands.push_back(std::move(box));
    return result;
}

IrNode Materialization::storage_load(SymbolId referent, const Span& span) const {
    const auto& target = symbol(referent);
    const auto& storage = storage_.at(referent);
    if (target.kind != SymbolKind::Field)
        return operation(IrOp::Load, span, storage.box->type, storage.slot);
    auto result = operation(IrOp::Field, span, storage.box->type, storage.slot);
    result.operands.push_back(operation(IrOp::Receiver, span, symbol(target.owner).type, target.owner));
    return result;
}

SymbolId Materialization::temporary(SymbolId owner, std::string name, const IrNode& initializer) {
    IrSymbol local{};
    local.kind = SymbolKind::Local;
    local.name = std::move(name);
    local.owner = owner;
    local.span = initializer.span;
    local.type = initializer.type;
    const auto id = program_.symbols.size();
    program_.symbols.push_back(std::move(local));
    return id;
}

void Materialization::plan() {
    for (auto& [id, storage] : storage_) {
        const auto kind = symbol(id).kind;
        const auto owner = symbol(id).owner;
        const auto span = symbol(id).span;
        const auto& box = *storage.box;
        if (symbol(id).type != symbol(box.value).type || symbol(id).constant ||
            (kind != SymbolKind::Local && kind != SymbolKind::Parameter &&
             kind != SymbolKind::Field && kind != SymbolKind::Global)) {
            throw Diagnostic(span, "invalid primitive reference storage").with_code("E0899");
        }
        if (kind == SymbolKind::Parameter) {
            const auto initial = operation(IrOp::Load, span, symbol(id).type, id);
            auto value = allocate(box, span, {initial});
            storage.slot = temporary(owner, "$reference_" + symbol(id).name, value);
            preparations_[owner].push_back(initialize(storage.slot, std::move(value), parameter_preparation));
        } else {
            program_.symbols[id].type = box.type;
            if (kind == SymbolKind::Field || kind == SymbolKind::Global) {
                const auto marker = kind == SymbolKind::Field ? field_preparation : global_preparation;
                preparations_[owner].push_back(initialize(id, allocate(box, span), marker));
            }
        }
    }
}

void Materialization::address(IrNode& place, const Storage& storage, SymbolId owner) {
    if (place.op == IrOp::ScopedValue) {
        for (auto it = place.body.begin(); it != place.body.end() - 1; ++it)
            rewrite(*it, owner);
        address(place.body.back(), storage, owner);
    } else {
        for (auto& operand : place.operands) rewrite(operand, owner);
        select_box(place, storage);
    }
    place.type = storage.box->type;
}

void Materialization::select_box(IrNode& place, const Storage& storage) const {
    const auto& slot = symbol(storage.slot);
    // A private module member belongs to its declaring module even when an
    // inherited method runs with a derived receiver. Module qualifiers have
    // no runtime evaluation to preserve.
    if (slot.kind == SymbolKind::Global && !slot.exported && !place.operands.empty() &&
        place.operands.front().type.kind == TypeKind::Module) {
        place.operands.clear();
    }
    place.symbol = storage.slot;
    place.type = storage.box->type;
}

void Materialization::rewrite_initializer(IrNode& node, const Storage& storage, SymbolId owner) {
    const auto kind = symbol(node.symbol).kind;
    if (kind == SymbolKind::Local) {
        auto allocation = allocate(*storage.box, node.span, std::move(node.operands));
        node.operands.clear();
        node.operands.push_back(std::move(allocation));
        return;
    }
    // The storage itself has already been prepared. Evaluate the value at
    // the original initialization site and fill the existing box.
    auto source = std::move(node.operands.front());
    auto destination = value_field(storage_load(node.symbol, node.span));
    node.op = IrOp::Store;
    node.symbol = no_symbol;
    // Only an originally literal global is visible before startup.
    if (kind == SymbolKind::Global && literal_primitive(source)) node.text = global_value;
    node.operands = {std::move(destination), std::move(source)};
    if (kind == SymbolKind::Field) preserve_field_store_order(node, owner);
}

void Materialization::preserve_field_store_order(IrNode& node, SymbolId owner) {
    // A source field write evaluates the receiver and the value before it
    // touches the field. Loading its box must not fault before value effects.
    auto& receiver = node.operands[0].operands.front().operands.front();
    auto& value = node.operands[1];
    const auto receiver_slot = temporary(owner, "$reference_receiver", receiver);
    const auto value_slot = temporary(owner, "$reference_value", value);
    auto saved_receiver = initialize(receiver_slot, std::move(receiver));
    auto saved_value = initialize(value_slot, std::move(value));
    receiver = operation(IrOp::Load, saved_receiver.span, symbol(receiver_slot).type, receiver_slot);
    value = operation(IrOp::Load, saved_value.span, symbol(value_slot).type, value_slot);
    auto block = operation(IrOp::Block, node.span);
    block.body.push_back(std::move(saved_receiver));
    block.body.push_back(std::move(saved_value));
    block.body.push_back(std::move(node));
    node = std::move(block);
}

void Materialization::rewrite(IrNode& node, SymbolId owner) {
    if (node.op == IrOp::ReferencePlace) {
        auto place = std::move(node.operands.front());
        address(place, storage_.at(node.symbol), owner);
        place.span = node.span;
        node = std::move(place);
        return;
    }
    const bool field_store = node.op == IrOp::Store && node.operands.front().op == IrOp::Field &&
        storage_.contains(node.operands.front().symbol);
    for (auto* region : {&node.operands, &node.body, &node.alternative})
        for (auto& child : *region) rewrite(child, owner);
    if (field_store) {
        preserve_field_store_order(node, owner);
    } else if (const auto found = storage_.find(node.symbol); found != storage_.end()) {
        if (node.op == IrOp::Initialize) {
            rewrite_initializer(node, found->second, owner);
        } else if (node.op == IrOp::Load || node.op == IrOp::Field) {
            select_box(node, found->second);
            node = value_field(std::move(node));
        }
    }
}

void Materialization::run() {
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& entry = symbol(id);
        if (entry.kind == SymbolKind::Class && is_primitive_reference(entry.type))
            boxes_.emplace(id, Box{entry.type});
    }
    if (boxes_.empty()) return;
    for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
        const auto& entry = symbol(id);
        const auto box = boxes_.find(entry.owner);
        if (box == boxes_.end()) continue;
        if (entry.kind == SymbolKind::Field && entry.name == "$value") {
            box->second.value = id;
        } else if (entry.kind == SymbolKind::Function && entry.name == "init") {
            if (entry.parameters.empty()) box->second.empty = id;
            else if (entry.parameters.size() == 1) box->second.initialized = id;
        }
    }
    for (const auto& body : program_.bodies)
        for (const auto& node : body.instructions) collect(node);
    if (storage_.empty()) return;
    include_global_overrides();
    plan();
    for (auto& body : program_.bodies) {
        for (auto& node : body.instructions) rewrite(node, body.owner);
        if (const auto found = preparations_.find(body.owner); found != preparations_.end()) {
            auto& prefix = found->second;
            body.instructions.insert(body.instructions.begin(),
                                     std::make_move_iterator(prefix.begin()), std::make_move_iterator(prefix.end()));
            preparations_.erase(found);
        }
    }
    for (auto& [owner, instructions] : preparations_)
        program_.bodies.push_back({owner, std::move(instructions)});
}

} // namespace

bool is_primitive_reference(const IrType& type) {
    return type.kind == TypeKind::Object && type.name.starts_with('&');
}

void materialize_reference_storage(IrProgram& program) {
    Materialization(program).run();
}

ReferencePreparation reference_preparation(const IrNode& node) {
    if (node.op == IrOp::Initialize && node.operands.size() == 1 &&
        node.operands.front().op == IrOp::Allocate && is_primitive_reference(node.operands.front().type)) {
        if (node.text == parameter_preparation) return ReferencePreparation::Parameter;
        if (node.text == field_preparation) return ReferencePreparation::Field;
        if (node.text == global_preparation) return ReferencePreparation::GlobalBox;
    }
    if (node.op == IrOp::Store && node.text == global_value && node.operands.size() == 2 &&
        node.operands.front().op == IrOp::Field && literal_primitive(node.operands.back()))
        return ReferencePreparation::GlobalValue;
    return ReferencePreparation::None;
}

} // namespace neura
