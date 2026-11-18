// FunctionCompiler's exit work and generator resumption
// (docs/design/Lifetime030.md, "Exit order" and "Lowering and artifacts").
// Included by converter.cpp, inside its namespace, after the class
// definitions; cleanup_stack.hpp describes the stack.

// A return evaluates and admits its value once, moves any array it carries
// into the caller's region, then runs the exit work of every block it
// leaves, innermost first, then returns the value, which a cleanup's return
// list may have replaced through Pending.
void FunctionCompiler::return_statement(const IrNode& node) {
    ExitSpec exit;
    exit.kind = nvr::ExitKind::Return;
    if (!node.operands.empty()) {
        auto value = values(node.operands.front());
        if (origin_.role == Role::Function) {
            value = fit_reference(std::move(value), c_.symbol(origin_.symbol).type);
            if (hook_owner(c_.symbol(origin_.symbol)) != no_symbol)
                value = normalize_hook_result(std::move(value), node.operands.front(), node.span);
        }
        if (result_region_slot_ != absent) {
            for (auto& word : value) spill(word);
            const auto nothing = c_.scalar(nvr::TypeKind::Void);
            for (const auto& word : value) {
                if (c_.type_at(word.type).kind == nvr::TypeKind::Array) {
                    state(nvr::StateOp::MoveToRegion, 1, {resolve(word), slot_operand(result_region_slot_)},
                          nothing, node.span);
                }
            }
        }
        if (has_cleanup()) {
            // The value lives in slots while the exit work runs, so that a
            // return list can read and replace it through Pending.
            std::vector<std::uint32_t> types;
            for (const auto& word : value) types.push_back(word.type);
            const auto slot = new_slots(types);
            for (std::size_t i = 0; i < value.size(); ++i) assign(slot + static_cast<std::uint32_t>(i), value[i]);
            const auto saved = std::exchange(pending_, PendingSlots{slot, value.size()});
            leave_scopes(0, node.span, ExitEdge::Return);
            pending_ = saved;
            // A return list that returned itself has closed the path.
            if (!open()) return;
            value = slot_values(slot, value.size());
        } else if (std::ranges::any_of(exit_stack_, [](const auto& scope) {
                       return scope.token || scope.kind == ExitWorkKind::TaskScope;
                   })) {
            // Commit the return value before leaving the scopes. Any array
            // result has already moved to the caller's region.
            for (auto& word : value) spill(word);
            leave_scopes(0, node.span, ExitEdge::Return);
            for (auto& word : value) word = resolve(word);
        }
        exit.arguments = std::move(value);
    } else {
        leave_scopes(0, node.span, ExitEdge::Return);
        if (!open()) return;
    }
    operation_at(node.span);
    close({exit});
}

bool FunctionCompiler::has_cleanup(std::size_t depth) const {
    return std::any_of(exit_stack_.begin() + static_cast<std::ptrdiff_t>(depth), exit_stack_.end(),
                       [](const auto& entry) { return entry.kind == ExitWorkKind::Cleanup; });
}

// While a Cleanup's list is emitted the stack holds only the entries below
// it, so a Return in the list (a failed close) is a return edge of the
// enclosing blocks alone; the stack is then restored for the other paths.
void FunctionCompiler::leave_scopes(std::size_t depth, const Span& span, ExitEdge edge) {
    for (auto i = exit_stack_.size(); i > depth && open(); --i) {
        const auto entry = exit_stack_[i - 1];
        if (entry.kind != ExitWorkKind::Cleanup) {
            exit_work(entry, span, edge);
            continue;
        }
        auto saved = exit_stack_;
        exit_stack_.resize(i - 1);
        exit_work(entry, span, edge);
        exit_stack_ = std::move(saved);
    }
}

void FunctionCompiler::exit_work(const ExitWork& entry, const Span& span, ExitEdge edge) {
    switch (entry.kind) {
    case ExitWorkKind::Region:
        release_region(entry, span);
        return;
    case ExitWorkKind::TaskScope:
        scheduler(nvr::SchedulerOp::ScopeExit, 0, {}, c_.scalar(nvr::TypeKind::Void), span);
        return;
    case ExitWorkKind::Cleanup: {
        const auto& node = *entry.node;
        region(edge == ExitEdge::Return && !node.alternative.empty() ? node.alternative : node.body);
        return;
    }
    }
}

// A block that allocates in a region (docs/design/Storage030.md) releases it
// after its cleanups have run and its task scope has joined.
void FunctionCompiler::release_region(const ExitWork& entry, const Span& span) {
    if (entry.token)
        state(nvr::StateOp::ReleaseRegion, 0, {*entry.token}, c_.scalar(nvr::TypeKind::Void), span);
}

Values FunctionCompiler::pending_values(const IrNode& node) {
    if (!pending_) throw Diagnostic(node.span, "a pending value outside a return's exit work").with_code("E0801");
    return slot_values(pending_->first, pending_->count);
}

// An operation the converter builds itself, with no operands yet.
static IrNode synthetic_node(IrOp op, IrType type, const Span& span) {
    IrNode node;
    node.op = op;
    node.type = std::move(type);
    node.span = span;
    return node;
}

// A generator's `$resume` (generator.resume names the field of its resume
// index) starts by comparing the index with 1 to n in ascending order and
// jumping to the block after that Yield; index 0 starts the body.
void FunctionCompiler::resume_chain() {
    const auto& function = c_.symbol(origin_.symbol);
    const auto* resume = find_attribute(function, AttributeId::GeneratorResume);
    if (!resume) return;
    resume_field_ = resume->value.symbol;
    const auto count = count_yields(c_.body(origin_.symbol));
    resume_blocks_.clear();
    for (std::size_t i = 0; i < count; ++i) resume_blocks_.push_back(new_block());
    if (count == 0) return;
    const auto& field = c_.symbol(resume_field_);
    auto receiver = synthetic_node(IrOp::Receiver, c_.symbol(field.owner).type, function.span);
    receiver.symbol = field.owner;
    auto read = synthetic_node(IrOp::Field, field.type, function.span);
    read.symbol = resume_field_;
    read.operands.push_back(std::move(receiver));
    auto index = expression(read);
    spill(index);
    const auto type = c_.type_of(field.type, function.span);
    const auto boolean = c_.scalar(nvr::TypeKind::Bool);
    for (std::size_t k = 1; k <= count; ++k) {
        const auto same = equal(current().synth.circuit(), lanes(index), lanes(immediate(type, k)));
        const auto next = new_block();
        branch(bits(boolean, {same}), resume_blocks_[k - 1], next);
        begin(next, function.span);
    }
}

// `yield` has stored its value in the buffer. The Yield stores its index
// and returns true; the next `$resume` continues in the block after it.
void FunctionCompiler::yield_statement(const IrNode& node) {
    std::size_t index = 0;
    for (const char digit : node.text) index = index * 10 + static_cast<std::size_t>(digit - '0');
    if (resume_field_ == no_symbol || index == 0 || index > resume_blocks_.size())
        throw Diagnostic(node.span, "a yield outside a generator's resume function").with_code("E0801");
    const auto& field = c_.symbol(resume_field_);
    auto receiver = synthetic_node(IrOp::Receiver, c_.symbol(field.owner).type, node.span);
    receiver.symbol = field.owner;
    auto place = synthetic_node(IrOp::Field, field.type, node.span);
    place.symbol = resume_field_;
    place.operands.push_back(std::move(receiver));
    auto number = synthetic_node(IrOp::Integer, field.type, node.span);
    number.text = node.text;
    auto save = synthetic_node(IrOp::Store, {}, node.span);
    save.operands.push_back(std::move(place));
    save.operands.push_back(std::move(number));
    store(save);
    ExitSpec exit;
    exit.kind = nvr::ExitKind::Return;
    exit.arguments.push_back(immediate(c_.scalar(nvr::TypeKind::Bool), 1));
    operation_at(node.span);
    close({exit});
    begin(resume_blocks_[index - 1], node.span);
}
