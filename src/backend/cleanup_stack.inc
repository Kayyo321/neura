// FunctionCompiler's exit work and generator resumption
// (docs/design/Lifetime030.md, "Exit order" and "Lowering and artifacts").
// Included by converter.cpp, inside its namespace, after the class
// definitions; cleanup_stack.hpp describes the stack.

// A return evaluates and admits its value once, then runs the exit work of
// every block it leaves, innermost first, then returns the value, which a
// cleanup's return list may have replaced through Pending. With no exit work
// open this emits what a return always did.
void FunctionCompiler::return_statement(const IrNode& node) {
    ExitSpec exit;
    exit.kind = nvr::ExitKind::Return;
    if (!node.operands.empty()) {
        auto value = values(node.operands.front());
        if (!exit_stack_.empty()) {
            // The value is computed first, then each block's exit work runs
            // (each task scope waits for its tasks, Concurrency.md 4.1), then
            // the function returns it.
            const auto slot = new_slots(c_.word_types(node.operands.front().type, node.span));
            for (std::size_t i = 0; i < value.size(); ++i) assign(slot + static_cast<std::uint32_t>(i), value[i]);
            const auto saved = std::exchange(pending_, PendingSlots{slot, value.size()});
            leave_cleanups(0, node.span, ExitEdge::Return);
            pending_ = saved;
            // A return list that returned itself has closed the path.
            if (!open()) return;
            value = slot_values(slot, value.size());
        }
        exit.arguments = std::move(value);
    } else {
        leave_cleanups(0, node.span, ExitEdge::Return);
        if (!open()) return;
    }
    operation_at(node.span);
    close({exit});
}

// While an entry's list is emitted the stack holds only the entries below
// it, so a Return in the list (a failed close) is a return edge of the
// enclosing blocks alone; the stack is then restored for the other paths.
void FunctionCompiler::leave_cleanups(std::size_t depth, const Span& span, ExitEdge edge) {
    for (auto i = exit_stack_.size(); i > depth && open(); --i) {
        const auto entry = exit_stack_[i - 1];
        if (entry.kind == ExitWorkKind::TaskScope) {
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

void FunctionCompiler::end_region(std::size_t depth, const Span& span) {
    if (exit_stack_.size() <= depth) return;
    if (open()) leave_cleanups(depth, span, ExitEdge::Fall);
    exit_stack_.resize(depth);
}

// The hook of the storage milestone (docs/design/Storage030.md): when the
// converter emits regions, a block that allocates in one pushes a Region
// entry before its ScopeEnter, and its release is emitted here, after the
// block's cleanups and the join of its task scope. Nothing pushes a Region
// entry yet.
void FunctionCompiler::release_region(const ExitWork&, const Span&) {}

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
