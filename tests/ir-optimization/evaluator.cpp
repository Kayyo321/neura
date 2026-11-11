#include "evaluator.hpp"

#include "neura-ir/host_binding.hpp"
#include "neura-ir/nominal.hpp"
#include "neura-ir/reference_storage.hpp"

#include "float_reference.hpp"
#include "support.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <limits>
#include <sstream>
#include <utility>

namespace neura::testing {
namespace {

using U64 = std::uint64_t;

struct Format {
    unsigned bits;
    bool is_signed;
};

// The evaluator keeps its own table rather than sharing the compiler's.
std::optional<Format> format_of(const IrType& type) {
    static const std::map<std::string, Format, std::less<>> formats{
        {"int8", {8, true}},   {"int16", {16, true}},   {"int32", {32, true}},   {"int64", {64, true}},
        {"uint8", {8, false}}, {"uint16", {16, false}}, {"uint32", {32, false}}, {"uint64", {64, false}},
        {"int", {32, true}},   {"byte", {8, false}},
    };
    if (type.kind != TypeKind::Integer) {
        return {};
    }
    const auto found = formats.find(type.name);
    return found == formats.end() ? std::nullopt : std::optional{found->second};
}

// An exact integer as sign and magnitude. `wide` marks a magnitude of at least
// 2^64, which no supported type can represent.
struct Exact {
    bool negative = false;
    U64 magnitude = 0;
    bool wide = false;
};

Exact normalized(Exact value) {
    if (value.magnitude == 0 && !value.wide) {
        value.negative = false;
    }
    return value;
}

Exact add(Exact a, Exact b) {
    if (a.wide || b.wide) {
        return {false, 0, true};
    }
    if (a.negative == b.negative) {
        const U64 sum = a.magnitude + b.magnitude; // Unsigned addition wraps.
        return normalized({a.negative, sum, sum < a.magnitude});
    }
    if (a.magnitude >= b.magnitude) {
        return normalized({a.negative, a.magnitude - b.magnitude, false});
    }
    return normalized({b.negative, b.magnitude - a.magnitude, false});
}

Exact negated(Exact value) {
    value.negative = !value.negative;
    return normalized(value);
}

U64 word_mask(unsigned bits) {
    return bits == 64 ? std::numeric_limits<U64>::max() : (U64{1} << bits) - 1;
}

U64 word(const Value& value) {
    return value.negative ? U64{0} - value.magnitude : value.magnitude;
}

// Forms the full 128-bit product of the magnitudes from 32-bit limbs.
Exact multiply(Exact a, Exact b) {
    constexpr U64 mask = 0xffffffffU;
    const U64 a_low = a.magnitude & mask;
    const U64 a_high = a.magnitude >> 32;
    const U64 b_low = b.magnitude & mask;
    const U64 b_high = b.magnitude >> 32;
    const U64 low_low = a_low * b_low;
    const U64 high_low = a_high * b_low;
    const U64 low_high = a_low * b_high;
    const U64 high_high = a_high * b_high;
    const U64 middle = (low_low >> 32) + (high_low & mask) + low_high;
    const U64 high = high_high + (high_low >> 32) + (middle >> 32);
    const U64 low = (middle << 32) | (low_low & mask);
    return normalized({a.negative != b.negative, low, high != 0 || a.wide || b.wide});
}

bool fits(const Exact& value, Format format) {
    if (value.wide) {
        return false;
    }
    if (!format.is_signed) {
        return !value.negative &&
               (format.bits == 64 || value.magnitude <= (U64{1} << format.bits) - 1);
    }
    const U64 limit = U64{1} << (format.bits - 1);
    return value.negative ? value.magnitude <= limit : value.magnitude < limit;
}

int compare(const Exact& a, const Exact& b) {
    if (a.negative != b.negative) {
        return a.negative ? -1 : 1;
    }
    const int order = a.magnitude < b.magnitude ? -1 : (a.magnitude > b.magnitude ? 1 : 0);
    return a.negative ? -order : order;
}

Exact exact(const Value& value) {
    return {value.negative, value.magnitude, false};
}

struct Stop {
    Status status;
    std::string reason;
    Span location;
    std::uint32_t trap_reason = 0;
    std::uint64_t exit_status = 0;
};

// Unwinds a task's interpretation when the run ended in another task. It is
// not a Stop: it carries no outcome.
struct Abandon {};

enum class Flow { Next, Return, Break, Continue };

struct Frame {
    SymbolId function = no_symbol;
    SymbolId receiver = no_symbol;
    // The object a method, constructor, or destructor runs on.
    std::optional<Value> self;
    std::map<SymbolId, Value> locals;
    std::optional<Value> result;
    // Scopes this activation has open, innermost last (indexes into the
    // machine's scope table).
    std::vector<std::size_t> scopes;
};

} // namespace

struct HeapEntry {
    enum class Kind { Object, Array, List, Tuple };
    Kind kind = Kind::Object;
    // Runtime class of an object.
    SymbolId cls = no_symbol;
    std::map<SymbolId, Value> fields;
    // Elements of an array, a list, or a tuple.
    std::vector<Value> elements;
    std::uint64_t version = 0;
    bool freed = false;
    // Class identity number, assigned at the first default hash or text
    // request (Types.md, Umbrella class and nil); zero until then.
    std::uint64_t identity = 0;
};

struct Heap {
    std::vector<HeapEntry> entries;
};

Value bool_value(bool value) {
    Value result;
    result.kind = Value::Kind::Bool;
    result.type = {TypeKind::Bool, "bool", no_symbol, {}};
    result.boolean = value;
    return result;
}

Value integer_value(const IrType& type, bool negative, std::uint64_t magnitude) {
    Value result;
    result.kind = Value::Kind::Integer;
    result.type = type;
    result.negative = negative && magnitude != 0;
    result.magnitude = magnitude;
    return result;
}

std::string describe(const Value& value) {
    switch (value.kind) {
    case Value::Kind::Void: return "void";
    case Value::Kind::Bool: return value.boolean ? "true" : "false";
    case Value::Kind::Integer:
        return (value.negative ? "-" : "") + std::to_string(value.magnitude) + ":" + value.type.name;
    case Value::Kind::Float: {
        char buffer[32];
        std::snprintf(buffer, sizeof buffer, "0x%llx", static_cast<unsigned long long>(value.magnitude));
        return std::string(buffer) + ":" + value.type.name;
    }
    case Value::Kind::String: return '"' + value.text + '"';
    case Value::Kind::Variant: {
        auto text = "variant %" + std::to_string(value.symbol);
        if (!value.payload.empty()) {
            text += "(";
            for (std::size_t i = 0; i < value.payload.size(); ++i) {
                text += (i == 0 ? "" : ", ") + describe(value.payload[i]);
            }
            text += ")";
        }
        return text;
    }
    case Value::Kind::Module: return "module %" + std::to_string(value.symbol);
    case Value::Kind::Reference: return "ref#" + std::to_string(value.handle);
    }
    return "<invalid>";
}

std::string describe(Status status) {
    switch (status) {
    case Status::Returned: return "returned";
    case Status::Trapped: return "trapped";
    case Status::Faulted: return "faulted";
    case Status::Unsupported: return "unsupported";
    case Status::LimitReached: return "limit reached";
    case Status::Exited: return "exited";
    }
    return "<invalid>";
}

std::string describe(const Outcome& outcome) {
    std::ostringstream out;
    out << describe(outcome.status);
    if (outcome.result) {
        out << ' ' << describe(*outcome.result);
    }
    if (outcome.status == Status::Exited) {
        out << " with status " << outcome.exit_status;
    }
    if (outcome.status != Status::Returned && outcome.status != Status::Exited) {
        out << " (" << outcome.reason;
        if (outcome.status == Status::Trapped) {
            out << ", reason " << outcome.trap_reason;
        }
        out << " at " << testing::describe(outcome.location) << ')';
    }
    out << ", effects [";
    for (std::size_t i = 0; i < outcome.effects.size(); ++i) {
        out << (i == 0 ? "" : ", ") << outcome.effects[i].callee << '(';
        for (std::size_t j = 0; j < outcome.effects[i].arguments.size(); ++j) {
            out << (j == 0 ? "" : ", ") << describe(outcome.effects[i].arguments[j]);
        }
        out << ')';
    }
    out << "], " << outcome.globals.size() << " global(s)";
    if (!outcome.schedule.empty()) {
        out << ", schedule [";
        for (std::size_t i = 0; i < outcome.schedule.size(); ++i) {
            out << (i == 0 ? "" : " ") << outcome.schedule[i];
        }
        out << ']';
    }
    return out.str();
}

std::string difference(const Outcome& left, const Outcome& right) {
    if (left.status != right.status) return "status differs";
    if (left.result != right.result) return "result differs";
    if (left.status != Status::Returned &&
        (left.reason != right.reason || left.trap_reason != right.trap_reason ||
         !same_span(left.location, right.location))) {
        return "stop reason or location differs";
    }
    if (left.exit_status != right.exit_status) return "exit status differs";
    if (left.effects != right.effects) return "effects differ";
    if (left.schedule != right.schedule) {
        std::size_t tick = 0;
        while (tick < left.schedule.size() && tick < right.schedule.size() &&
               left.schedule[tick] == right.schedule[tick]) {
            ++tick;
        }
        return "schedules differ at tick " + std::to_string(tick) + " (" + std::to_string(left.schedule.size()) +
               " and " + std::to_string(right.schedule.size()) + " decisions)";
    }
    if (left.globals != right.globals) return "final global state differs";
    return {};
}

class Evaluator::Machine {
public:
    Machine(const IrProgram& program, Limits limits, std::map<SymbolId, Value> globals, Heap heap)
        : program_(program), limits_(limits), globals_(std::move(globals)), heap_(std::move(heap)) {
        for (const auto& body : program.bodies) {
            bodies_.emplace(body.owner, &body);
            if (program.symbols.at(body.owner).kind == SymbolKind::Class) {
                for (const auto& instruction : body.instructions) {
                    if (reference_preparation(instruction) == ReferencePreparation::Field) {
                        reference_fields_[body.owner].push_back(&instruction);
                    } else {
                        field_initializers_[body.owner].push_back(&instruction);
                    }
                }
            }
        }
        for (SymbolId id = 0; id < program.symbols.size(); ++id) {
            if (program.symbols[id].kind == SymbolKind::Function) {
                functions_by_owner_[program.symbols[id].owner].push_back(id);
            }
        }
    }

    const std::map<SymbolId, Value>& globals() const { return globals_; }
    const Heap& heap() const { return heap_; }

    Outcome run_body(const IrBody& body, SymbolId receiver) {
        return guarded([&]() -> std::optional<Value> {
            Frame frame;
            frame.receiver = receiver;
            run_region(body.instructions, frame);
            return std::nullopt;
        });
    }

    Outcome call(SymbolId function, const std::vector<Value>& arguments) {
        return guarded([&]() -> std::optional<Value> {
            return invoke(function, no_symbol, arguments, symbol(function).span);
        });
    }

    Outcome call_entry(SymbolId function) {
        return guarded([&]() -> std::optional<Value> {
            const auto& parameters = symbol(function).parameters;
            if (parameters.size() != 1) {
                throw Stop{Status::Unsupported, "an entry that does not take one argument list", symbol(function).span};
            }
            heap_.entries.push_back({HeapEntry::Kind::Array, no_symbol, {}, {}, 0, false, 0});
            Value empty;
            empty.kind = Value::Kind::Reference;
            empty.type = symbol(parameters.front()).type;
            empty.handle = heap_.entries.size() - 1;
            return invoke(function, no_symbol, {empty}, symbol(function).span);
        });
    }

    Outcome evaluate_closed(const IrNode& expression) {
        return guarded([&]() -> std::optional<Value> {
            Frame frame;
            return evaluate(expression, frame);
        });
    }

private:
    const IrProgram& program_;
    Limits limits_;
    std::map<SymbolId, Value> globals_;
    Heap heap_;
    std::map<SymbolId, const IrBody*> bodies_;
    std::map<SymbolId, std::vector<const IrNode*>> field_initializers_;
    std::map<SymbolId, std::vector<const IrNode*>> reference_fields_;
    std::map<SymbolId, std::vector<SymbolId>> functions_by_owner_;
    std::vector<Effect> effects_;
    static constexpr std::size_t nil_handle = std::numeric_limits<std::size_t>::max();
    U64 identities_ = 0;
    mutable std::map<std::pair<SymbolId, std::string>, SymbolId> members_;
    std::size_t spent_ = 0;
    std::size_t depth_ = 0;

    const IrSymbol& symbol(SymbolId id) const { return program_.symbols.at(id); }

    // A variant's tag: its position among its enum's variants.
    std::size_t variant_position(SymbolId variant) const {
        const auto owner = symbol(variant).owner;
        std::size_t position = 0;
        for (SymbolId id = 0; id < variant; ++id) {
            if (symbol(id).kind == SymbolKind::Variant && symbol(id).owner == owner) ++position;
        }
        return position;
    }

    // ---- Reference scheduler (Concurrency.md section 10.2) ----
    //
    // One thread per started task; `turn_` names the task allowed to run and
    // every other thread waits on its own condition variable. All scheduler
    // state is touched only by the task holding the turn, and each hand-off
    // goes through `mutex_`, which orders the memory.

    enum class TaskState { Runnable, Running, Joining, Leaving, Finished };

    struct TaskRecord {
        std::uint64_t id = 0;
        std::uint64_t parent = 0;
        // Index in scopes_ of the scope that owns this task.
        std::size_t scope = 0;
        // A discarded spawn has no handle and keeps no result.
        bool handle = true;
        bool started = false;
        TaskState state = TaskState::Runnable;
        std::uint64_t waiting_for = 0;
        std::size_t leaving = 0;
        std::optional<Value> result;
        Value function;
        SymbolId method = no_symbol;
        Span site;
        std::condition_variable wake;
    };

    struct ScopeRecord {
        std::uint64_t owner = 0;
        bool open = true;
        std::size_t live = 0;
    };

    std::mutex mutex_;
    bool ended_ = false;
    std::uint64_t turn_ = 0;
    std::uint64_t current_ = 0;
    std::uint64_t next_task_ = 1;
    std::size_t live_tasks_ = 1;
    std::size_t polls_ = 0;
    std::deque<std::uint64_t> runnable_;
    std::map<std::uint64_t, std::unique_ptr<TaskRecord>> tasks_;
    std::vector<ScopeRecord> scopes_;
    std::map<std::uint64_t, std::thread> threads_;
    std::vector<std::uint64_t> finished_threads_;
    std::vector<std::uint64_t> schedule_;
    std::optional<Stop> stop_;

    void reset_scheduler() {
        ended_ = false;
        turn_ = current_ = 0;
        next_task_ = 1;
        live_tasks_ = 1;
        polls_ = 0;
        runnable_.clear();
        tasks_.clear();
        scopes_.clear();
        threads_.clear();
        finished_threads_.clear();
        schedule_.clear();
        stop_.reset();
        auto root = std::make_unique<TaskRecord>();
        root->state = TaskState::Running;
        root->started = true;
        tasks_.emplace(0, std::move(root));
    }

    TaskRecord& task(std::uint64_t id) { return *tasks_.at(id); }

    // Ends the run: no task runs again, and every waiting thread unwinds.
    // Called with the lock held by the task that stopped.
    void end_run(const std::lock_guard<std::mutex>&) {
        ended_ = true;
        for (auto& [id, record] : tasks_) record->wake.notify_all();
    }

    // Takes the front of the runnable queue as the next task (one tick) and
    // hands it the turn, starting its thread on its first turn. The caller
    // then waits for its own turn or leaves.
    void choose_next() {
        if (runnable_.empty()) {
            throw Stop{Status::Faulted, "deadlock: all tasks are parked", {}};
        }
        const auto next = runnable_.front();
        runnable_.pop_front();
        schedule_.push_back(next);
        polls_ = 0;
        turn_ = current_ = next;
        auto& record = task(next);
        record.state = TaskState::Running;
        if (!record.started) {
            record.started = true;
            threads_.emplace(next, std::thread([this, &record] { run_task(record); }));
        } else {
            record.wake.notify_one();
        }
    }

    void await_turn(std::unique_lock<std::mutex>& lock, TaskRecord& me) {
        me.wake.wait(lock, [&] { return ended_ || turn_ == me.id; });
        if (ended_) throw Abandon{};
    }

    // Joins the threads of finished tasks. Each has already handed over its
    // turn, so this waits at most for the thread to leave its function.
    void reap() {
        std::vector<std::thread> done;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            for (const auto id : finished_threads_) {
                done.push_back(std::move(threads_.at(id)));
                threads_.erase(id);
            }
            finished_threads_.clear();
        }
        for (auto& thread : done) thread.join();
    }

    // The running task stops running until it is chosen again. The caller has
    // set its state and, for a yield, put it on the queue.
    void suspend(TaskRecord& me) {
        const auto depth = depth_;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            choose_next();
            await_turn(lock, me);
        }
        depth_ = depth;
        reap();
    }

    void run_task(TaskRecord& me) {
        try {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                await_turn(lock, me);
            }
            depth_ = 0;
            reap();
            auto result = invoke(me.method, no_symbol, {}, me.site, me.function);
            finish_task(me, std::move(result));
        } catch (const Abandon&) {
        } catch (const Stop& stop) {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!stop_) stop_ = stop;
            end_run(lock);
        } catch (const std::exception& error) {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!stop_) stop_ = Stop{Status::Unsupported, std::string("internal error: ") + error.what(), me.site};
            end_run(lock);
        }
    }

    void finish_task(TaskRecord& me, Value result) {
        const std::lock_guard<std::mutex> lock(mutex_);
        me.state = TaskState::Finished;
        if (me.handle) me.result = std::move(result);
        --live_tasks_;
        auto& scope = scopes_.at(me.scope);
        --scope.live;
        auto& parent = task(me.parent);
        if (parent.state == TaskState::Joining && parent.waiting_for == me.id) {
            parent.state = TaskState::Runnable;
            runnable_.push_back(parent.id);
        } else if (parent.state == TaskState::Leaving && parent.leaving == me.scope && scope.live == 0) {
            parent.state = TaskState::Runnable;
            runnable_.push_back(parent.id);
        }
        finished_threads_.push_back(me.id);
        choose_next();
    }

    // A poll counts one toward the running task's quantum. A task that has
    // spent it goes to the back of the queue when another task is runnable.
    void poll() {
        if (++polls_ < limits_.quantum) return;
        if (runnable_.empty()) {
            polls_ = 0;
            return;
        }
        auto& me = task(current_);
        me.state = TaskState::Runnable;
        runnable_.push_back(me.id);
        suspend(me);
    }

    Value spawn_task(const IrNode& node, Frame& frame, bool handle) {
        const auto function = evaluate(node.operands.at(0), frame);
        if (node.operands.front().op != IrOp::Allocate || function.kind != Value::Kind::Reference) {
            unsupported(node, "a spawn whose operand is not the allocation of a capture layout");
        }
        if (frame.scopes.empty()) {
            fault(node, "invalid task operation: spawn outside a task scope of its function");
        }
        if (live_tasks_ + 1 > limits_.max_tasks) {
            throw Stop{Status::LimitReached, "task limit", node.span};
        }
        const auto method = own_member(object(function, node).cls, "$invoke");
        if (method == no_symbol || !symbol(method).has_body) {
            unsupported(node, "a spawned function value without a body");
        }
        auto record = std::make_unique<TaskRecord>();
        record->id = next_task_++;
        record->parent = current_;
        record->scope = frame.scopes.back();
        record->handle = handle;
        record->function = function;
        record->method = method;
        record->site = node.span;
        const auto id = integer_value(node.type, false, record->id);
        ++scopes_.at(record->scope).live;
        ++live_tasks_;
        runnable_.push_back(record->id);
        tasks_.emplace(record->id, std::move(record));
        return id;
    }

    // The record behind a task number, if the current task may use it: only
    // the task that owns the scope, and only while the scope is open.
    TaskRecord& owned_task(const Value& id, const IrNode& node) {
        const auto found = id.kind == Value::Kind::Integer && !id.negative ? tasks_.find(id.magnitude) : tasks_.end();
        if (found == tasks_.end() || found->first == 0 || !found->second->handle) {
            fault(node, "invalid task operation: unknown task handle");
        }
        const auto& scope = scopes_.at(found->second->scope);
        if (!scope.open || scope.owner != current_) {
            fault(node, "invalid task operation: task handle is outside its owning scope");
        }
        return *found->second;
    }

    // Every exit from a scope's body waits for the tasks it owns, then closes
    // the scope and, with it, their handles.
    void close_scope(Frame& frame) {
        const auto index = frame.scopes.back();
        if (scopes_.at(index).live > 0) {
            auto& me = task(current_);
            me.state = TaskState::Leaving;
            me.leaving = index;
            suspend(me);
        }
        scopes_.at(index).open = false;
        frame.scopes.pop_back();
    }

    // Stops every task and joins every thread.
    void shut_down() {
        std::map<std::uint64_t, std::thread> threads;
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            end_run(lock);
            threads.swap(threads_);
        }
        for (auto& [id, thread] : threads) {
            if (thread.joinable()) thread.join();
        }
    }

    template <typename Run>
    Outcome guarded(Run run) {
        effects_.clear();
        spent_ = 0;
        depth_ = 0;
        reset_scheduler();
        Outcome outcome;
        try {
            outcome.result = run();
        } catch (const Stop& stop) {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (!stop_) stop_ = stop;
        } catch (const Abandon&) {
            // Another task ended the run.
        } catch (...) {
            shut_down();
            throw;
        }
        shut_down();
        if (stop_) {
            outcome.result.reset();
            outcome.status = stop_->status;
            outcome.reason = stop_->reason;
            outcome.trap_reason = stop_->trap_reason;
            outcome.location = stop_->location;
            outcome.exit_status = stop_->exit_status;
        }
        outcome.effects = effects_;
        outcome.globals = globals_;
        outcome.schedule = schedule_;
        return outcome;
    }

    [[noreturn]] static void unsupported(const IrNode& node, std::string reason) {
        throw Stop{Status::Unsupported, std::move(reason), node.span};
    }

    [[noreturn]] static void trap(const IrNode& node, std::uint32_t reason, std::string description) {
        throw Stop{Status::Trapped, std::move(description), node.span, reason};
    }

    [[noreturn]] static void fault(const IrNode& node, std::string description) {
        throw Stop{Status::Faulted, std::move(description), node.span};
    }

    void spend(const Span& where) {
        if (++spent_ > limits_.budget) {
            throw Stop{Status::LimitReached, "loop and call budget exhausted", where};
        }
    }

    Value invoke(SymbolId function, SymbolId receiver, const std::vector<Value>& arguments, const Span& site,
                 std::optional<Value> self = std::nullopt) {
        spend(site);
        if (depth_ >= limits_.depth) {
            throw Stop{Status::LimitReached, "call depth limit", site};
        }
        const auto body = bodies_.find(function);
        if (body == bodies_.end()) {
            throw Stop{Status::Unsupported, "call to " + qualified_name(program_, function) + " without a body", site};
        }
        Frame frame;
        frame.function = function;
        frame.receiver = receiver;
        frame.self = std::move(self);
        const auto& parameters = symbol(function).parameters;
        if (parameters.size() != arguments.size()) {
            throw Stop{Status::Unsupported, "argument count mismatch", site};
        }
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            frame.locals[parameters[i]] = arguments[i];
        }
        ++depth_;
        run_region(body->second->instructions, frame);
        --depth_;
        return frame.result.value_or(Value{});
    }

    // The implementation a call on `module` selects for `target`: the first
    // function in the module or its bases that is `target` or overrides it.
    SymbolId dispatch(SymbolId target, SymbolId module) const {
        std::size_t steps = 0;
        for (auto scope = module; scope < program_.symbols.size() && steps++ < program_.symbols.size();
             scope = symbol(scope).base) {
            const auto found = functions_by_owner_.find(scope);
            if (found == functions_by_owner_.end()) {
                continue;
            }
            for (const auto candidate : found->second) {
                for (auto current = candidate; current < program_.symbols.size(); current = symbol(current).overrides) {
                    if (current == target) {
                        return candidate;
                    }
                }
            }
        }
        return target;
    }

    Flow run_region(const std::vector<IrNode>& region, Frame& frame) {
        for (const auto& statement : region) {
            const auto flow = run_statement(statement, frame);
            if (flow != Flow::Next) {
                return flow;
            }
        }
        return Flow::Next;
    }

    Flow run_statement(const IrNode& node, Frame& frame) {
        switch (node.op) {
        case IrOp::Initialize: {
            auto value = evaluate(node.operands.at(0), frame);
            const auto kind = symbol(node.symbol).kind;
            if (kind == SymbolKind::Local) {
                frame.locals[node.symbol] = std::move(value);
            } else if (kind == SymbolKind::Global) {
                globals_[node.symbol] = std::move(value);
            } else {
                object(*frame.self, node).fields[node.symbol] = std::move(value);
            }
            return Flow::Next;
        }
        case IrOp::Store: {
            const auto& destination = node.operands.at(0);
            if (destination.op == IrOp::Field) {
                // The receiver is evaluated before the stored value.
                const auto receiver = evaluate(destination.operands.at(0), frame);
                auto value = evaluate(node.operands.at(1), frame);
                object(receiver, node).fields[destination.symbol] = std::move(value);
                return Flow::Next;
            }
            if (destination.op == IrOp::Index) {
                const auto collection = evaluate(destination.operands.at(0), frame);
                const auto index = evaluate(destination.operands.at(1), frame);
                auto value = evaluate(node.operands.at(1), frame);
                auto& entry = collection_entry(collection, node);
                entry.elements[element_index(entry, index, destination)] = std::move(value);
                return Flow::Next;
            }
            if (destination.op != IrOp::Load) {
                unsupported(destination, "store destination");
            }
            const bool global = symbol(destination.symbol).kind == SymbolKind::Global;
            if (global) {
                evaluate_global_qualifier(destination, frame);
            }
            auto value = evaluate(node.operands.at(1), frame);
            (global ? globals_ : frame.locals)[destination.symbol] = std::move(value);
            return Flow::Next;
        }
        case IrOp::Discard:
            if (node.operands.at(0).op == IrOp::Spawn) {
                // The task has no handle: nothing can join it.
                spawn_task(node.operands.at(0), frame, false);
                return Flow::Next;
            }
            evaluate(node.operands.at(0), frame);
            return Flow::Next;
        case IrOp::TaskScope: {
            scopes_.push_back({current_, true, 0});
            frame.scopes.push_back(scopes_.size() - 1);
            // Falling off the end, return (its value already evaluated),
            // break, and continue all leave through the same wait.
            const auto flow = run_region(node.body, frame);
            close_scope(frame);
            return flow;
        }
        case IrOp::Block:
            return run_region(node.body, frame);
        case IrOp::Branch:
            return run_region(condition(node.operands.at(0), frame) ? node.body : node.alternative, frame);
        case IrOp::Loop:
            while (condition(node.operands.at(0), frame)) {
                spend(node.span);
                const auto flow = run_region(node.body, frame);
                if (flow == Flow::Return) {
                    return flow;
                }
                if (flow == Flow::Break) {
                    break;
                }
                // The latch runs after the body completes or continues.
                run_region(node.alternative, frame);
                if (node.poll) poll();
            }
            return Flow::Next;
        case IrOp::Fail:
            trap(node, 5, "collection modified during iteration");
        case IrOp::TrapMessage:
            trap(node, 3, evaluate(node.operands.at(0), frame).text);
        case IrOp::Destroy: {
            const auto target = evaluate(node.operands.at(0), frame);
            const auto cls = object(target, node).cls;
            destruct(cls, target, node.span);
            object(target, node).freed = true;
            return Flow::Next;
        }
        case IrOp::Return:
            if (!node.operands.empty()) {
                frame.result = evaluate(node.operands.front(), frame);
            }
            return Flow::Return;
        case IrOp::Break:
            return Flow::Break;
        case IrOp::Continue:
            return Flow::Continue;
        default:
            unsupported(node, "statement operation");
        }
    }

    bool condition(const IrNode& node, Frame& frame) {
        const auto value = evaluate(node, frame);
        if (value.kind != Value::Kind::Bool) {
            unsupported(node, "non-bool condition");
        }
        return value.boolean;
    }

    Value module_value(SymbolId module) const {
        Value result;
        result.kind = Value::Kind::Module;
        result.type = symbol(module).type;
        result.symbol = module;
        return result;
    }

    void evaluate_global_qualifier(const IrNode& load, Frame& frame) {
        if (load.operands.empty()) {
            return;
        }
        const auto receiver = evaluate(load.operands.front(), frame);
        if (load.operands.front().type.kind != TypeKind::Module) return;
        if (receiver.kind != Value::Kind::Module || receiver.symbol != symbol(load.symbol).owner) {
            unsupported(load, "global accessed through another module");
        }
    }

    Value literal(const IrNode& node) const {
        std::string_view text = node.text;
        const bool negative = text.starts_with('-');
        if (negative) {
            text.remove_prefix(1);
        }
        U64 magnitude = 0;
        for (const char digit : text) {
            const auto d = static_cast<U64>(digit - '0');
            if (digit < '0' || digit > '9' || magnitude > (std::numeric_limits<U64>::max() - d) / 10) {
                unsupported(node, "malformed integer literal");
            }
            magnitude = magnitude * 10 + d;
        }
        return integer_value(node.type, negative, magnitude);
    }

    std::vector<Value> arguments(const IrNode& node, std::size_t first, Frame& frame) {
        std::vector<Value> values;
        for (std::size_t i = first; i < node.operands.size(); ++i) {
            values.push_back(evaluate(node.operands[i], frame));
        }
        return values;
    }

    Value evaluate(const IrNode& node, Frame& frame) {
        switch (node.op) {
        case IrOp::Boolean:
            return bool_value(node.text == "true");
        case IrOp::Integer:
            return literal(node);
        case IrOp::String: {
            Value result;
            result.kind = Value::Kind::String;
            result.type = node.type;
            result.text = node.text;
            return result;
        }
        case IrOp::Load: {
            const auto kind = symbol(node.symbol).kind;
            if (kind == SymbolKind::Global) {
                evaluate_global_qualifier(node, frame);
                const auto found = globals_.find(node.symbol);
                if (found == globals_.end()) {
                    unsupported(node, "read of an uninitialized global");
                }
                return found->second;
            }
            const auto found = frame.locals.find(node.symbol);
            if (found == frame.locals.end()) {
                unsupported(node, "read of an unset local");
            }
            return found->second;
        }
        case IrOp::Reference: {
            const auto& target = symbol(node.symbol);
            if (target.kind == SymbolKind::Variant) {
                Value result;
                result.kind = Value::Kind::Variant;
                result.type = node.type;
                result.symbol = node.symbol;
                return result;
            }
            if (target.kind == SymbolKind::Module) {
                return module_value(node.symbol);
            }
            if (target.kind == SymbolKind::Import && target.type.kind == TypeKind::Module) {
                return module_value(target.type.symbol);
            }
            unsupported(node, "reference to a non-value symbol");
        }
        case IrOp::Receiver:
            if (symbol(node.symbol).kind != SymbolKind::Module) {
                if (!frame.self) {
                    unsupported(node, "object receiver outside a method");
                }
                return *frame.self;
            }
            return module_value(frame.receiver != no_symbol ? frame.receiver : node.symbol);
        case IrOp::Field: {
            const auto receiver = evaluate(node.operands.at(0), frame);
            auto& entry = object(receiver, node);
            const auto found = entry.fields.find(node.symbol);
            if (found == entry.fields.end()) {
                fault(node, "read of an uninitialized field");
            }
            return found->second;
        }
        case IrOp::Allocate: {
            auto values = arguments(node, 0, frame);
            // The allocated class owns the constructor the node names; a
            // specialized closure is typed by its function signature but is
            // an instance of its own closure class.
            const auto cls = node.symbol != no_symbol && symbol(node.symbol).kind == SymbolKind::Function
                ? symbol(node.symbol).owner : node.type.symbol;
            heap_.entries.push_back({HeapEntry::Kind::Object, cls, {}, {}, 0, false, 0});
            Value result;
            result.kind = Value::Kind::Reference;
            result.type = node.type;
            result.handle = heap_.entries.size() - 1;
            prepare_reference_fields(cls, result);
            construct(cls, node.symbol, result, values, node.span);
            return result;
        }
        case IrOp::RepeatCollection:
        case IrOp::Collection: {
            auto values = arguments(node, 0, frame);
            if (node.op == IrOp::RepeatCollection) {
                const auto count = values[0].magnitude;
                if (count > limits_.budget || count > values.max_size())
                    throw Stop{Status::LimitReached, "collection allocation exceeds evaluation budget", node.span};
                const auto fill = std::move(values[1]);
                values.assign(static_cast<std::size_t>(count), fill);
            }
            const auto kind = node.type.kind == TypeKind::List ? HeapEntry::Kind::List : HeapEntry::Kind::Array;
            heap_.entries.push_back({kind, no_symbol, {}, std::move(values), 0, false, 0});
            Value result;
            result.kind = Value::Kind::Reference;
            result.type = node.type;
            result.handle = heap_.entries.size() - 1;
            return result;
        }
        case IrOp::Index: {
            const auto collection = evaluate(node.operands.at(0), frame);
            const auto index = evaluate(node.operands.at(1), frame);
            auto& entry = collection_entry(collection, node);
            return entry.elements[element_index(entry, index, node)];
        }
        case IrOp::Length: {
            const auto collection = evaluate(node.operands.at(0), frame);
            return integer_value(node.type, false, collection_entry(collection, node).elements.size());
        }
        case IrOp::CollectionVersion: {
            const auto collection = evaluate(node.operands.at(0), frame);
            return integer_value(node.type, false, collection_entry(collection, node).version);
        }
        case IrOp::Append: {
            const auto collection = evaluate(node.operands.at(0), frame);
            auto value = evaluate(node.operands.at(1), frame);
            auto& entry = collection_entry(collection, node);
            entry.elements.push_back(std::move(value));
            ++entry.version;
            return Value{};
        }
        case IrOp::RemoveAt: {
            const auto collection = evaluate(node.operands.at(0), frame);
            const auto index = evaluate(node.operands.at(1), frame);
            auto& entry = collection_entry(collection, node);
            const auto position = element_index(entry, index, node);
            entry.elements.erase(entry.elements.begin() + static_cast<std::ptrdiff_t>(position));
            ++entry.version;
            return Value{};
        }
        case IrOp::Convert:
            return convert(node, evaluate(node.operands.at(0), frame));
        case IrOp::BaseInitialize: {
            auto values = arguments(node, 1, frame);
            const auto self = evaluate(node.operands.at(0), frame);
            construct(symbol(owner_class(frame)).base, node.symbol, self, values, node.span);
            return Value{};
        }
        case IrOp::Unary:
            return unary(node, evaluate(node.operands.at(0), frame));
        case IrOp::Binary: {
            auto left = evaluate(node.operands.at(0), frame);
            auto right = evaluate(node.operands.at(1), frame);
            return binary(node, left, right);
        }
        case IrOp::ShortCircuit: {
            const bool left = condition(node.operands.at(0), frame);
            if (node.text == "&&" ? !left : left) {
                return bool_value(left);
            }
            return bool_value(condition(node.body.at(0), frame));
        }
        case IrOp::ComparisonChain:
            run_statement(node.body.at(0), frame);
            return evaluate(node.body.at(1), frame);
        case IrOp::Call: {
            auto values = arguments(node, 0, frame);
            if (node.poll) poll();
            return invoke(node.symbol, no_symbol, std::move(values), node.span);
        }
        case IrOp::Spawn:
            return spawn_task(node, frame, true);
        case IrOp::SchedulerCall: {
            if (node.text != "join" && node.text != "is_finished") {
                unsupported(node, "scheduler operation " + node.text);
            }
            const auto id = evaluate(node.operands.at(0), frame);
            auto& child = owned_task(id, node);
            if (node.text == "is_finished") {
                return bool_value(child.state == TaskState::Finished);
            }
            if (child.state != TaskState::Finished) {
                auto& me = task(current_);
                me.state = TaskState::Joining;
                me.waiting_for = child.id;
                suspend(me);
            }
            return child.result.value_or(Value{});
        }
        case IrOp::VirtualCall: {
            const auto receiver = evaluate(node.operands.at(0), frame);
            auto values = arguments(node, 1, frame);
            if (node.poll) poll();
            if (receiver.kind == Value::Kind::Reference) {
                // Dispatch on the runtime class, by name, as overrides match.
                const auto cls = object(receiver, node).cls;
                return invoke(implementation(cls, node.symbol, node), no_symbol, values, node.span, receiver);
            }
            if (receiver.kind != Value::Kind::Module) {
                unsupported(node, "method call on this receiver");
            }
            return invoke(dispatch(node.symbol, receiver.symbol), receiver.symbol, values, node.span);
        }
        case IrOp::ExternalCall: {
            auto values = arguments(node, 0, frame);
            effects_.push_back({qualified_name(program_, node.symbol), values});
            if (const auto& binding = symbol(node.symbol).host_binding;
                binding && binding->name == "neura.process.exit") {
                // The host ends the run; no task continues.
                Stop exit{Status::Exited, "process exit", node.span};
                exit.exit_status = values.at(0).magnitude;
                throw exit;
            }
            const auto& result = symbol(node.symbol).type;
            if (result.kind == TypeKind::Void) {
                return Value{};
            }
            // A host binding answers with its success variant. Only a
            // payload-free Ok can be modeled without a provider.
            const auto variants = host_variants(program_, result);
            if (!variants || !symbol(variants->success).payload_types.empty()) {
                unsupported(node, "external call with a result");
            }
            Value ok;
            ok.kind = Value::Kind::Variant;
            ok.type = result;
            ok.symbol = variants->success;
            return ok;
        }
        case IrOp::EnumConstruct: {
            Value result;
            result.kind = Value::Kind::Variant;
            result.type = node.type;
            result.symbol = node.symbol;
            result.payload = arguments(node, 0, frame);
            return result;
        }
        case IrOp::EnumTag: {
            const auto value = evaluate(node.operands.at(0), frame);
            if (value.kind != Value::Kind::Variant) {
                unsupported(node, "enum tag of a value that is not a variant");
            }
            return integer_value(node.type, false, variant_position(value.symbol));
        }
        case IrOp::EnumProject: {
            const auto value = evaluate(node.operands.at(0), frame);
            const auto index = static_cast<std::size_t>(std::stoul(node.text));
            if (value.kind != Value::Kind::Variant || index >= value.payload.size()) {
                unsupported(node, "projection of a missing variant field");
            }
            return value.payload[index];
        }
        case IrOp::ScopedValue: {
            for (std::size_t i = 0; i + 1 < node.body.size(); ++i) {
                run_statement(node.body[i], frame);
            }
            return evaluate(node.body.back(), frame);
        }
        case IrOp::SelectValue:
            return evaluate(condition(node.operands.at(0), frame) ? node.body.at(0) : node.alternative.at(0), frame);
        case IrOp::TrapMessage:
            trap(node, 3, evaluate(node.operands.at(0), frame).text);
        case IrOp::Nil: {
            Value result;
            result.kind = Value::Kind::Reference;
            result.type = node.type;
            result.handle = nil_handle;
            return result;
        }
        case IrOp::Tuple: {
            auto values = arguments(node, 0, frame);
            heap_.entries.push_back({HeapEntry::Kind::Tuple, no_symbol, {}, std::move(values), 0, false, 0});
            Value result;
            result.kind = Value::Kind::Reference;
            result.type = node.type;
            result.handle = heap_.entries.size() - 1;
            return result;
        }
        case IrOp::Project: {
            const auto tuple = evaluate(node.operands.at(0), frame);
            const auto& item = entry(tuple, node);
            const auto index = static_cast<std::size_t>(std::stoul(node.text));
            if (item.kind != HeapEntry::Kind::Tuple || index >= item.elements.size()) {
                unsupported(node, "projection of a value that is not a tuple");
            }
            return item.elements[index];
        }
        case IrOp::TextLength: {
            const auto text = evaluate(node.operands.at(0), frame);
            return integer_value(node.type, false, text.text.size());
        }
        case IrOp::TextByteAt: {
            const auto text = evaluate(node.operands.at(0), frame);
            const auto index = evaluate(node.operands.at(1), frame);
            if (index.negative) trap(node, 2, "negative index");
            if (index.magnitude >= text.text.size()) fault(node, "index out of bounds");
            return integer_value(node.type, false, static_cast<unsigned char>(text.text[index.magnitude]));
        }
        case IrOp::TextSlice: {
            const auto text = evaluate(node.operands.at(0), frame);
            const auto start = evaluate(node.operands.at(1), frame);
            const auto end = evaluate(node.operands.at(2), frame);
            if (start.negative || end.negative) trap(node, 2, "negative index");
            const auto& bytes = text.text;
            if (start.magnitude > end.magnitude || end.magnitude > bytes.size()) fault(node, "text range out of bounds");
            const auto boundary = [&](U64 offset) {
                return offset == bytes.size() || (static_cast<unsigned char>(bytes[offset]) & 0xc0U) != 0x80U;
            };
            if (!boundary(start.magnitude) || !boundary(end.magnitude)) {
                fault(node, "text slice endpoints must be on UTF-8 boundaries");
            }
            Value result = text;
            result.text = bytes.substr(start.magnitude, end.magnitude - start.magnitude);
            return result;
        }
        case IrOp::TextEqual: {
            const auto left = evaluate(node.operands.at(0), frame);
            const auto right = evaluate(node.operands.at(1), frame);
            // The node's text is the source operator, == or !=.
            return bool_value((left.text == right.text) == (node.text != "!="));
        }
        case IrOp::TextFromUtf8: {
            const auto source = evaluate(node.operands.at(0), frame);
            const auto& item = collection_entry(source, node);
            std::string bytes;
            for (const auto& element : item.elements) bytes.push_back(static_cast<char>(element.magnitude));
            const bool valid = valid_utf8(bytes);
            Value text;
            text.kind = Value::Kind::String;
            text.type = node.type.arguments.at(1);
            text.text = valid ? bytes : std::string();
            heap_.entries.push_back({HeapEntry::Kind::Tuple, no_symbol, {}, {bool_value(valid), text}, 0, false, 0});
            Value result;
            result.kind = Value::Kind::Reference;
            result.type = node.type;
            result.handle = heap_.entries.size() - 1;
            return result;
        }
        case IrOp::ReinterpretFloat: {
            auto value = evaluate(node.operands.at(0), frame);
            value.kind = node.type.kind == TypeKind::Float ? Value::Kind::Float : Value::Kind::Integer;
            value.type = node.type;
            value.negative = false;
            return value;
        }
        case IrOp::ClassIs: {
            const auto value = evaluate(node.operands.at(0), frame);
            if (value.handle == nil_handle) return bool_value(false);
            return bool_value(instance_of(object(value, node).cls, node.type_arguments.at(0)));
        }
        case IrOp::ClassCast: {
            auto value = evaluate(node.operands.at(0), frame);
            if (value.handle != nil_handle && !instance_of(object(value, node).cls, node.type)) {
                fault(node, "invalid class cast");
            }
            value.type = node.type;
            return value;
        }
        case IrOp::ClassHash: {
            const auto value = evaluate(node.operands.at(0), frame);
            return integer_value(node.type, false, (identity(value, node) * U64{2654435761}) & 0xffffffffU);
        }
        case IrOp::ClassText: {
            const auto value = evaluate(node.operands.at(0), frame);
            const auto number = identity(value, node);
            Value result;
            result.kind = Value::Kind::String;
            result.type = node.type;
            result.text = qualified_class(object(value, node).cls) + "#" + std::to_string(number);
            return result;
        }
        case IrOp::ValueEquals: {
            const auto left = evaluate(node.operands.at(0), frame);
            const auto right = evaluate(node.operands.at(1), frame);
            return bool_value(value_equals(left, right, node.operands.at(0).type, node));
        }
        case IrOp::ValueHash: {
            const auto value = evaluate(node.operands.at(0), frame);
            return integer_value(node.type, false, value_hash(value, node.operands.at(0).type, node));
        }
        case IrOp::ValueText: {
            const auto value = evaluate(node.operands.at(0), frame);
            Value result;
            result.kind = Value::Kind::String;
            result.type = node.type;
            result.text = value_text(value, node.operands.at(0).type, node);
            return result;
        }
        case IrOp::Float: {
            const auto format = float_of(node.type, node);
            // strtod and strtof round correctly; for the 16-bit formats the
            // double is rounded again.
            const auto text = node.text;
            if (format.width() == 32) {
                const float value = std::strtof(text.c_str(), nullptr);
                std::uint32_t bits = 0;
                std::memcpy(&bits, &value, 4);
                return float_value(node.type, bits);
            }
            return float_value(node.type, floats::encode(std::strtod(text.c_str(), nullptr), format));
        }
        default:
            unsupported(node, "expression operation");
        }
    }

    static floats::Format float_of(const IrType& type, const IrNode& node) {
        if (type.name == "float16") return {5, 10};
        if (type.name == "bfloat16") return {8, 7};
        if (type.name == "float32") return {8, 23};
        if (type.name == "float64") return {11, 52};
        unsupported(node, "float type " + type.name);
    }

    static Value float_value(const IrType& type, std::uint64_t bits) {
        Value result;
        result.kind = Value::Kind::Float;
        result.type = type;
        result.magnitude = bits;
        return result;
    }

    Value unary(const IrNode& node, const Value& operand) {
        if (node.text == "!" && operand.kind == Value::Kind::Bool) {
            return bool_value(!operand.boolean);
        }
        if (node.text == "-" && operand.kind == Value::Kind::Float) {
            const auto format = float_of(operand.type, node);
            return float_value(operand.type, operand.magnitude ^ (std::uint64_t{1} << (format.width() - 1)));
        }
        if (node.text == "~" && operand.kind == Value::Kind::Integer) {
            return word_value(node, ~word(operand));
        }
        if (node.text != "-" || operand.kind != Value::Kind::Integer) {
            unsupported(node, "unary operation");
        }
        return arithmetic_result(node, negated(exact(operand)));
    }

    Value word_value(const IrNode& node, U64 bits) {
        const auto format = format_of(node.type);
        if (!format) {
            unsupported(node, "bitwise operation on " + node.type.name);
        }
        const auto mask = word_mask(format->bits);
        bits &= mask;
        const bool negative = format->is_signed && ((bits >> (format->bits - 1)) != 0);
        return integer_value(node.type, negative, negative ? ((~bits + 1) & mask) : bits);
    }

    Value arithmetic_result(const IrNode& node, const Exact& result) {
        const auto format = format_of(node.type);
        if (!format) {
            unsupported(node, "arithmetic on " + node.type.name);
        }
        if (!fits(result, *format)) {
            trap(node, 1, "overflow");
        }
        return integer_value(node.type, result.negative, result.magnitude);
    }

    Value convert(const IrNode& node, const Value& operand) {
        if (node.type.kind == TypeKind::Float) {
            const auto to = float_of(node.type, node);
            if (operand.kind == Value::Kind::Float) {
                return float_value(node.type, floats::convert(operand.magnitude, float_of(operand.type, node), to));
            }
            // The conversion to double rounds correctly; a second rounding
            // to a narrower format is innocuous (float_reference.hpp).
            const double magnitude = static_cast<double>(operand.magnitude);
            return float_value(node.type, floats::encode(operand.negative ? -magnitude : magnitude, to));
        }
        if (operand.kind == Value::Kind::Float) {
            const double value = floats::decode(operand.magnitude, float_of(operand.type, node));
            if (std::isnan(value) || std::isinf(value)) {
                trap(node, 1, "conversion of a NaN or an infinity");
            }
            const double truncated = std::trunc(value);
            if (std::fabs(truncated) >= 18446744073709551616.0) {
                trap(node, 1, "conversion out of range");
            }
            const Exact exact_value{truncated < 0, static_cast<U64>(std::fabs(truncated)), false};
            const auto format = format_of(node.type);
            if (!format) {
                unsupported(node, "conversion to " + node.type.name);
            }
            if (!fits(normalized(exact_value), *format)) {
                trap(node, 1, "conversion out of range");
            }
            return integer_value(node.type, exact_value.negative && exact_value.magnitude != 0, exact_value.magnitude);
        }
        Exact value;
        if (operand.kind == Value::Kind::Integer) {
            value = exact(operand);
        } else if (operand.kind == Value::Kind::Variant) {
            value.magnitude = ordinal(operand.symbol);
        } else {
            unsupported(node, "conversion of this operand");
        }
        const auto format = format_of(node.type);
        if (!format) {
            unsupported(node, "conversion to " + node.type.name);
        }
        if (!fits(value, *format)) {
            trap(node, 1, "conversion out of range");
        }
        return integer_value(node.type, value.negative, value.magnitude);
    }

    // Position of a variant among its enum's variants, in declaration order.
    U64 ordinal(SymbolId variant) const {
        U64 position = 0;
        for (SymbolId id = 0; id < variant; ++id) {
            if (symbol(id).kind == SymbolKind::Variant && symbol(id).owner == symbol(variant).owner) {
                ++position;
            }
        }
        return position;
    }

    HeapEntry& entry(const Value& value, const IrNode& node) {
        if (value.kind == Value::Kind::Reference && value.handle == nil_handle) {
            fault(node, "nil class reference");
        }
        if (value.kind != Value::Kind::Reference || value.handle >= heap_.entries.size()) {
            unsupported(node, "a reference operand that is not a reference");
        }
        auto& item = heap_.entries[value.handle];
        if (item.freed) {
            fault(node, "use of a destroyed object");
        }
        return item;
    }

    HeapEntry& object(const Value& value, const IrNode& node) {
        auto& item = entry(value, node);
        if (item.kind != HeapEntry::Kind::Object) {
            unsupported(node, "an object operand that is a collection");
        }
        return item;
    }

    HeapEntry& collection_entry(const Value& value, const IrNode& node) {
        auto& item = entry(value, node);
        if (item.kind == HeapEntry::Kind::Object) {
            unsupported(node, "a collection operand that is an object");
        }
        return item;
    }

    // A negative index is a program trap; one at or past the length is a
    // runtime bounds fault.
    std::size_t element_index(const HeapEntry& item, const Value& index, const IrNode& node) {
        if (index.kind != Value::Kind::Integer) {
            unsupported(node, "a non-integer index");
        }
        if (index.negative) {
            trap(node, 2, "negative index");
        }
        if (index.magnitude >= item.elements.size()) {
            fault(node, "index out of bounds");
        }
        return static_cast<std::size_t>(index.magnitude);
    }

    SymbolId own_member(SymbolId owner, std::string_view name) const {
        const auto key = std::make_pair(owner, std::string(name));
        if (const auto found = members_.find(key); found != members_.end()) return found->second;
        SymbolId result = no_symbol;
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            const auto& item = symbol(id);
            if (item.owner == owner && item.name == name &&
                (item.kind == SymbolKind::Function || item.kind == SymbolKind::Field)) {
                result = id;
                break;
            }
        }
        members_.emplace(key, result);
        return result;
    }

    // The method with `method`'s name that the runtime class or its nearest
    // base defines with a body.
    SymbolId implementation(SymbolId cls, SymbolId method, const IrNode& node) const {
        for (auto scope = cls; scope != no_symbol; scope = symbol(scope).base) {
            const auto found = own_member(scope, symbol(method).name);
            if (found != no_symbol && symbol(found).kind == SymbolKind::Function && symbol(found).has_body) {
                return found;
            }
        }
        // An interface default body, the most specific first: the class and
        // its bases come before the interfaces they implement.
        for (const auto& view : nominal_views(program_, symbol(cls).type)) {
            if (view.symbol >= program_.symbols.size() || symbol(view.symbol).kind != SymbolKind::Interface) continue;
            const auto found = own_member(view.symbol, symbol(method).name);
            if (found != no_symbol && symbol(found).kind == SymbolKind::Function && symbol(found).has_body) {
                return found;
            }
        }
        unsupported(node, "no implementation of " + symbol(method).name + " in " + symbol(cls).name);
    }

    static bool valid_utf8(const std::string& bytes) {
        std::size_t i = 0;
        while (i < bytes.size()) {
            const auto b0 = static_cast<unsigned char>(bytes[i]);
            std::size_t length = 0;
            std::uint32_t scalar = 0;
            if (b0 < 0x80U) { ++i; continue; }
            if ((b0 & 0xe0U) == 0xc0U) { length = 2; scalar = b0 & 0x1fU; }
            else if ((b0 & 0xf0U) == 0xe0U) { length = 3; scalar = b0 & 0x0fU; }
            else if ((b0 & 0xf8U) == 0xf0U) { length = 4; scalar = b0 & 0x07U; }
            else return false;
            if (i + length > bytes.size()) return false;
            for (std::size_t k = 1; k < length; ++k) {
                const auto b = static_cast<unsigned char>(bytes[i + k]);
                if ((b & 0xc0U) != 0x80U) return false;
                scalar = (scalar << 6) | (b & 0x3fU);
            }
            const std::uint32_t minimum = length == 2 ? 0x80U : length == 3 ? 0x800U : 0x10000U;
            if (scalar < minimum || scalar > 0x10ffffU || (scalar >= 0xd800U && scalar <= 0xdfffU)) return false;
            i += length;
        }
        return true;
    }

    // Whether an object of runtime class `cls` is an instance of `target`.
    bool instance_of(SymbolId cls, const IrType& target) const {
        if (target.kind == TypeKind::Object && target.symbol == root_class()) return true;
        return nominal_assignable(program_, symbol(cls).type, target);
    }

    SymbolId root_class() const {
        for (SymbolId id = 0; id < program_.symbols.size(); ++id) {
            if (symbol(id).kind == SymbolKind::Class && symbol(id).name == "$class") return id;
        }
        return no_symbol;
    }

    // The class name qualified by its modules, without the file.
    std::string qualified_class(SymbolId cls) const {
        std::string name = symbol(cls).name;
        for (auto owner = symbol(cls).owner; owner != no_symbol && symbol(owner).kind != SymbolKind::File;
             owner = symbol(owner).owner) {
            name = symbol(owner).name + "." + name;
        }
        return name;
    }

    U64 identity(const Value& value, const IrNode& node) {
        auto& item = object(value, node);
        if (item.identity == 0) item.identity = ++identities_;
        return item.identity;
    }

    // Calls the root class member `name` (equals, hash_code, or to_string) on
    // a live object, with dynamic dispatch.
    Value class_member(const Value& receiver, std::string_view name, std::vector<Value> arguments,
                       const IrNode& node) {
        const auto method = own_member(root_class(), name);
        if (method == no_symbol) unsupported(node, "no root class member " + std::string(name));
        const auto cls = object(receiver, node).cls;
        return invoke(implementation(cls, method, node), no_symbol, arguments, node.span, receiver);
    }

    static bool class_type(const IrType& type) {
        return type.kind == TypeKind::Object;
    }

    // Value member contracts (Stdlib.md section 5.2), written from the
    // specification rather than from the converter's circuits.
    bool value_equals(const Value& a, const Value& b, const IrType& type, const IrNode& node) {
        switch (type.kind) {
        case TypeKind::Bool: return a.boolean == b.boolean;
        case TypeKind::Integer: return a.negative == b.negative && a.magnitude == b.magnitude;
        case TypeKind::Float: {
            const auto format = float_of(type, node);
            return a.magnitude == b.magnitude || (floats::is_nan(a.magnitude, format) && floats::is_nan(b.magnitude, format));
        }
        case TypeKind::String: return a.text == b.text;
        case TypeKind::Enum:
            if (a.kind != Value::Kind::Variant || !a.payload.empty() || !b.payload.empty())
                unsupported(node, "value equality of an enum with payloads");
            return a.symbol == b.symbol;
        case TypeKind::Tuple:
        case TypeKind::Array:
        case TypeKind::List: {
            // An element's equals method can allocate, which moves the heap's
            // entries, and can change either collection. Each element is
            // therefore read again from the heap and compared as a copy.
            if (entry(a, node).elements.size() != entry(b, node).elements.size()) return false;
            for (std::size_t i = 0; i < entry(a, node).elements.size() && i < entry(b, node).elements.size(); ++i) {
                const auto& element = type.kind == TypeKind::Tuple ? type.arguments.at(i) : type.arguments.at(0);
                const Value left = entry(a, node).elements[i];
                const Value right = entry(b, node).elements[i];
                if (!value_equals(left, right, element, node)) return false;
            }
            return true;
        }
        case TypeKind::Object: {
            if (a.handle == nil_handle || b.handle == nil_handle) return a.handle == b.handle;
            auto other = b;
            other.type = IrType{TypeKind::Object, "$class", root_class(), {}};
            return class_member(a, "equals", {other}, node).boolean;
        }
        default:
            unsupported(node, "value equality of " + type.name);
        }
    }

    U64 value_hash(const Value& value, const IrType& type, const IrNode& node) {
        constexpr U64 low = 0xffffffffU;
        switch (type.kind) {
        case TypeKind::Bool: return value.boolean ? 1231 : 1237;
        case TypeKind::Integer: {
            const auto pattern = value.negative ? (~value.magnitude + 1) : value.magnitude;
            const auto format = format_of(type);
            if (format && format->bits == 64) return (pattern & low) ^ (pattern >> 32);
            return pattern & low;
        }
        case TypeKind::Float: {
            const auto format = float_of(type, node);
            auto bits = value.magnitude;
            const auto width = 1 + format.exponent_bits + format.fraction_bits;
            if (floats::is_nan(bits, format)) {
                bits = (((U64{1} << format.exponent_bits) - 1) << format.fraction_bits) |
                       (U64{1} << (format.fraction_bits - 1));
            }
            if (width == 64) return (bits & low) ^ (bits >> 32);
            return bits & low;
        }
        case TypeKind::String: {
            U64 hash = 0;
            for (const auto byte : value.text) hash = (hash * 31 + static_cast<unsigned char>(byte)) & low;
            return hash;
        }
        case TypeKind::Enum:
            if (value.kind != Value::Kind::Variant || !value.payload.empty())
                unsupported(node, "value hash of an enum with payloads");
            return variant_position(value.symbol);
        case TypeKind::Tuple:
        case TypeKind::Array:
        case TypeKind::List: {
            U64 hash = 1;
            // As in value_equals: hash_code can allocate or change the
            // collection, so no reference into the heap is kept across it.
            for (std::size_t i = 0; i < entry(value, node).elements.size(); ++i) {
                const auto& element = type.kind == TypeKind::Tuple ? type.arguments.at(i) : type.arguments.at(0);
                const Value item = entry(value, node).elements[i];
                hash = (hash * 31 + value_hash(item, element, node)) & low;
            }
            return hash;
        }
        case TypeKind::Object:
            if (value.handle == nil_handle) return 0;
            return class_member(value, "hash_code", {}, node).magnitude;
        default:
            unsupported(node, "value hash of " + type.name);
        }
    }

    std::string value_text(const Value& value, const IrType& type, const IrNode& node) {
        switch (type.kind) {
        case TypeKind::Bool: return value.boolean ? "true" : "false";
        case TypeKind::Integer: return (value.negative ? "-" : "") + std::to_string(value.magnitude);
        case TypeKind::Float: return float_text(value.magnitude, float_of(type, node));
        case TypeKind::String: return value.text;
        case TypeKind::Enum:
            if (value.kind != Value::Kind::Variant || !value.payload.empty())
                unsupported(node, "value text of an enum with payloads");
            return symbol(value.symbol).name;
        case TypeKind::Tuple:
        case TypeKind::Array:
        case TypeKind::List: {
            std::string text = "[";
            // As in value_equals: to_string can allocate or change the
            // collection, so no reference into the heap is kept across it.
            for (std::size_t i = 0; i < entry(value, node).elements.size(); ++i) {
                const auto& element = type.kind == TypeKind::Tuple ? type.arguments.at(i) : type.arguments.at(0);
                const Value item = entry(value, node).elements[i];
                text += (i == 0 ? "" : ", ") + value_text(item, element, node);
            }
            return text + "]";
        }
        case TypeKind::Object:
            if (value.handle == nil_handle) return "nil";
            return class_member(value, "to_string", {}, node).text;
        default:
            unsupported(node, "value text of " + type.name);
        }
    }

    // Shortest decimal text that reads back as the same value (Stdlib.md
    // section 7.5), from the C++ library's shortest round-trip conversion
    // for binary32 and binary64 and a search over printf precisions for the
    // 16-bit formats.
    static std::string float_text(U64 bits, floats::Format format) {
        const auto width = 1 + format.exponent_bits + format.fraction_bits;
        const bool negative = ((bits >> (width - 1)) & 1U) != 0;
        const auto magnitude = bits & ((U64{1} << (width - 1)) - 1);
        const std::string sign = negative ? "-" : "";
        if (floats::is_nan(bits, format)) return "NaN";
        const auto infinity = ((U64{1} << format.exponent_bits) - 1) << format.fraction_bits;
        if (magnitude == infinity) return sign + "Infinity";
        if (magnitude == 0) return sign + "0.0";
        char buffer[64];
        std::string scientific;
        if (width == 64) {
            double value = 0;
            std::memcpy(&value, &magnitude, sizeof value);
            const auto end = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific).ptr;
            scientific.assign(buffer, end);
        } else if (width == 32 && format.fraction_bits == 23) {
            const auto narrow = static_cast<std::uint32_t>(magnitude);
            float value = 0;
            std::memcpy(&value, &narrow, sizeof value);
            const auto end = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::scientific).ptr;
            scientific.assign(buffer, end);
        } else {
            // For each digit count, the correctly rounded decimal and its two
            // neighbors are the candidates; the shortest that reads back
            // wins, the closest of those, then the one with an even last
            // digit.
            const double value = floats::decode(magnitude, format);
            for (int precision = 0; precision < 17 && scientific.empty(); ++precision) {
                std::snprintf(buffer, sizeof buffer, "%.*e", precision, value);
                const std::string rounded = buffer;
                const auto mark = rounded.find('e');
                std::string mantissa = rounded.substr(0, mark);
                mantissa.erase(std::remove(mantissa.begin(), mantissa.end(), '.'), mantissa.end());
                const auto exponent = std::stoi(rounded.substr(mark + 1)) - precision;
                const auto center = std::stoull(mantissa);
                long double best_distance = -1;
                std::uint64_t best = 0;
                for (const auto candidate : {center - 1, center, center + 1}) {
                    if (candidate == 0 || (candidate == center - 1 && center == 0)) continue;
                    std::snprintf(buffer, sizeof buffer, "%llue%d", static_cast<unsigned long long>(candidate), exponent);
                    if (floats::encode(std::strtod(buffer, nullptr), format) != magnitude) continue;
                    const auto distance = std::fabs(std::strtold(buffer, nullptr) - static_cast<long double>(value));
                    // Two adjacent candidates tie exactly when the value is
                    // their midpoint, (2c - 1) * 5 * 10^(exponent - 1).
                    bool tie = false;
                    if (best_distance >= 0 && best + 1 == candidate) {
                        std::snprintf(buffer, sizeof buffer, "%llue%d",
                                      static_cast<unsigned long long>((2 * candidate - 1) * 5), exponent - 1);
                        tie = std::strtod(buffer, nullptr) == value;
                    }
                    if (best_distance < 0 || (tie ? candidate % 2 == 0 : distance < best_distance)) {
                        best_distance = distance;
                        best = candidate;
                    }
                }
                if (best_distance >= 0) {
                    auto digits = std::to_string(best);
                    const auto leading = exponent + static_cast<int>(digits.size()) - 1;
                    scientific = digits.substr(0, 1) + (digits.size() > 1 ? "." + digits.substr(1) : "") + "e" +
                                 std::to_string(leading);
                }
            }
        }
        const auto e = scientific.find('e');
        std::string digits = scientific.substr(0, e);
        digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
        const int exponent = std::stoi(scientific.substr(e + 1));
        while (digits.size() > 1 && digits.back() == '0') digits.pop_back();
        if (exponent < -4 || exponent >= 16) {
            return sign + digits.substr(0, 1) + "." + (digits.size() == 1 ? "0" : digits.substr(1)) + "e" +
                   std::to_string(exponent);
        }
        if (exponent < 0) return sign + "0." + std::string(static_cast<std::size_t>(-exponent - 1), '0') + digits;
        const auto split = static_cast<std::size_t>(exponent + 1);
        if (split >= digits.size()) return sign + digits + std::string(split - digits.size(), '0') + ".0";
        return sign + digits.substr(0, split) + "." + digits.substr(split);
    }

    SymbolId owner_class(const Frame& frame) const {
        return frame.function == no_symbol ? no_symbol : symbol(frame.function).owner;
    }

    // Construction order (Object lifecycle): the base, then this class's
    // field initializers, then the rest of its declared constructor.
    void construct(SymbolId cls, SymbolId init, const Value& self, const std::vector<Value>& arguments,
                   const Span& site) {
        spend(site);
        if (depth_ >= limits_.depth) {
            throw Stop{Status::LimitReached, "call depth limit", site};
        }
        ++depth_;
        if (init == no_symbol) {
            if (const auto group = functions_by_owner_.find(cls); group != functions_by_owner_.end()) {
                const auto& functions = group->second;
                const auto zero_argument = std::ranges::find_if(functions, [&](SymbolId candidate) {
                    const auto& function = symbol(candidate);
                    return function.name == "init" && function.has_body && function.parameters.empty();
                });
                if (zero_argument != functions.end()) init = *zero_argument;
            }
        }
        const auto base = symbol(cls).base;
        const bool has_base_initializer = base != no_symbol && symbol(base).name != "$class";
        Frame frame;
        frame.self = self;
        frame.function = init;
        if (init != no_symbol) {
            const auto& parameters = symbol(init).parameters;
            for (std::size_t i = 0; i < parameters.size() && i < arguments.size(); ++i) {
                frame.locals[parameters[i]] = arguments[i];
            }
            const auto& body = bodies_.at(init)->instructions;
            std::size_t start = 0;
            while (start < body.size() && reference_preparation(body[start]) == ReferencePreparation::Parameter) {
                run_statement(body[start], frame);
                ++start;
            }
            if (has_base_initializer && start < body.size()) {
                run_statement(body[start], frame);
                ++start;
            }
            initialize_fields(cls, self);
            for (std::size_t i = start; i < body.size(); ++i) {
                if (run_statement(body[i], frame) == Flow::Return) {
                    break;
                }
            }
        } else {
            if (has_base_initializer) {
                construct(base, no_symbol, self, {}, site);
            }
            initialize_fields(cls, self);
        }
        --depth_;
    }

    void prepare_reference_fields(SymbolId cls, const Value& self) {
        if (reference_fields_.empty() || cls == no_symbol) return;
        prepare_reference_fields(symbol(cls).base, self);
        const auto found = reference_fields_.find(cls);
        if (found == reference_fields_.end()) return;
        Frame frame;
        frame.self = self;
        for (const auto* preparation : found->second) run_statement(*preparation, frame);
    }

    void initialize_fields(SymbolId cls, const Value& self) {
        const auto found = field_initializers_.find(cls);
        if (found == field_initializers_.end()) {
            return;
        }
        Frame frame;
        frame.self = self;
        for (const auto* initializer : found->second) {
            run_statement(*initializer, frame);
        }
    }

    // Cleanup runs from the most derived class toward its bases.
    void destruct(SymbolId cls, const Value& self, const Span& site) {
        const auto cleanup = own_member(cls, "deinit");
        if (cleanup != no_symbol && symbol(cleanup).kind == SymbolKind::Function && symbol(cleanup).has_body) {
            invoke(cleanup, no_symbol, {}, site, self);
        }
        if (symbol(cls).base != no_symbol) {
            destruct(symbol(cls).base, self, site);
        }
    }

    Value binary(const IrNode& node, const Value& left, const Value& right) {
        const auto& op = node.text;
        if (left.kind == Value::Kind::Reference && right.kind == Value::Kind::Reference && (op == "==" || op == "!=")) {
            // Class references compare by identity; two nils are equal.
            return bool_value((left.handle == right.handle) == (op == "=="));
        }
        if (left.kind == Value::Kind::String && right.kind == Value::Kind::String && (op == "==" || op == "!=")) {
            return bool_value((left.text == right.text) == (op == "=="));
        }
        if (left.kind == Value::Kind::Bool && right.kind == Value::Kind::Bool && (op == "==" || op == "!=")) {
            return bool_value((left.boolean == right.boolean) == (op == "=="));
        }
        if (left.kind == Value::Kind::Variant && right.kind == Value::Kind::Variant && (op == "==" || op == "!=")) {
            return bool_value((left.symbol == right.symbol) == (op == "=="));
        }
        if (left.kind == Value::Kind::String && right.kind == Value::Kind::String && op == "+") {
            Value result = left;
            result.text += right.text;
            return result;
        }
        if (left.kind == Value::Kind::Float && right.kind == Value::Kind::Float) {
            const auto format = float_of(left.type, node);
            const auto a = left.magnitude;
            const auto b = right.magnitude;
            if (op == "==") return bool_value(floats::compare(floats::Compare::Equal, a, b, format));
            if (op == "!=") return bool_value(floats::compare(floats::Compare::NotEqual, a, b, format));
            if (op == "<") return bool_value(floats::compare(floats::Compare::Less, a, b, format));
            if (op == "<=") return bool_value(floats::compare(floats::Compare::LessEqual, a, b, format));
            if (op == ">") return bool_value(floats::compare(floats::Compare::Greater, a, b, format));
            if (op == ">=") return bool_value(floats::compare(floats::Compare::GreaterEqual, a, b, format));
            const auto operation = op == "+"   ? floats::Op::Add
                                   : op == "-" ? floats::Op::Subtract
                                   : op == "*" ? floats::Op::Multiply
                                               : floats::Op::Divide;
            return float_value(node.type, floats::binary(operation, a, b, format));
        }
        if (left.kind != Value::Kind::Integer || right.kind != Value::Kind::Integer) {
            unsupported(node, "binary operation on these operands");
        }
        if (op == "&") return word_value(node, word(left) & word(right));
        if (op == "|") return word_value(node, word(left) | word(right));
        if (op == "^") return word_value(node, word(left) ^ word(right));
        if (op == "<<" || op == ">>") {
            const auto format = format_of(left.type);
            if (!format) {
                unsupported(node, "shift of " + left.type.name);
            }
            const auto count = right.magnitude;
            if (right.negative || count >= format->bits) {
                trap(node, 1, "shift count out of range");
            }
            if (op == "<<") {
                return word_value(node, word(left) << count);
            }
            const auto divisor = U64{1} << count;
            const auto rounded = left.magnitude / divisor +
                (left.negative && left.magnitude % divisor != 0 ? 1 : 0);
            return integer_value(node.type, left.negative, rounded);
        }
        const auto a = exact(left);
        const auto b = exact(right);
        const int order = compare(a, b);
        if (op == "==") return bool_value(order == 0);
        if (op == "!=") return bool_value(order != 0);
        if (op == "<") return bool_value(order < 0);
        if (op == "<=") return bool_value(order <= 0);
        if (op == ">") return bool_value(order > 0);
        if (op == ">=") return bool_value(order >= 0);
        if (op == "+") return arithmetic_result(node, add(a, b));
        if (op == "-") return arithmetic_result(node, add(a, negated(b)));
        if (op == "*") return arithmetic_result(node, multiply(a, b));
        if (op == "/" || op == "%") {
            if (b.magnitude == 0) {
                trap(node, 1, "division by zero");
            }
            // Truncating division on magnitudes; the remainder takes the
            // dividend's sign.
            if (op == "/") {
                return arithmetic_result(node, normalized({a.negative != b.negative, a.magnitude / b.magnitude, false}));
            }
            return arithmetic_result(node, normalized({a.negative, a.magnitude % b.magnitude, false}));
        }
        unsupported(node, "binary operator " + op);
    }
};

namespace {

std::unique_ptr<IrProgram> materialized_copy(const IrProgram& program) {
    if (std::ranges::none_of(program.symbols, [](const IrSymbol& symbol) {
            return symbol.kind == SymbolKind::Class && is_primitive_reference(symbol.type);
        })) return {};
    auto result = std::make_unique<IrProgram>(program);
    materialize_reference_storage(*result);
    return result;
}

} // namespace

Evaluator::Evaluator(const IrProgram& program, Limits limits)
    : materialized_(materialized_copy(program)), program_(materialized_ ? *materialized_ : program), limits_(limits) {}

Evaluator::~Evaluator() = default;

std::vector<Outcome> Evaluator::initialize() {
    // Modules.md, Initialization: literal globals hold their values first.
    // Then the files run, each after the files it imports (ties in program
    // order), and each file's initializers and statements run in source
    // order.
    const auto& symbols = program_.symbols;
    const auto file_of = [&](SymbolId id) {
        while (id != no_symbol && symbols.at(id).kind != SymbolKind::File) {
            id = symbols.at(id).owner;
        }
        return id;
    };
    const auto literal = [&](const IrNode& node) {
        return node.op == IrOp::Integer || node.op == IrOp::Float || node.op == IrOp::Boolean ||
               node.op == IrOp::String || node.op == IrOp::Nil ||
               (node.op == IrOp::Reference && symbols.at(node.symbol).kind == SymbolKind::Variant);
    };
    std::vector<SymbolId> files;
    std::vector<SymbolId> order_storage;
    std::map<SymbolId, std::vector<SymbolId>> imports;
    for (SymbolId id = 0; id < symbols.size(); ++id) {
        if (symbols[id].kind == SymbolKind::File) {
            files.push_back(id);
        } else if (symbols[id].kind == SymbolKind::Import && symbols[id].type.symbol != no_symbol) {
            const auto target = file_of(symbols[id].type.symbol);
            if (target != no_symbol && target != file_of(id)) {
                imports[file_of(id)].push_back(target);
            }
        }
    }
    // Files that import each other (a cycle, possibly through other files)
    // start together once every file the cycle imports from outside it has
    // run; within the cycle they keep program order.
    const auto reaches = [&](SymbolId from, SymbolId to) {
        std::vector<SymbolId> stack{from};
        std::vector<SymbolId> seen;
        while (!stack.empty()) {
            const auto current = stack.back();
            stack.pop_back();
            for (const auto next : imports[current]) {
                if (next == to) return true;
                if (std::find(seen.begin(), seen.end(), next) == seen.end()) {
                    seen.push_back(next);
                    stack.push_back(next);
                }
            }
        }
        return false;
    };
    const auto together = [&](SymbolId a, SymbolId b) { return a == b || (reaches(a, b) && reaches(b, a)); };
    const auto done = [&](SymbolId file) { return std::find(order_storage.begin(), order_storage.end(), file) != order_storage.end(); };
    auto& order = order_storage;
    while (order.size() < files.size()) {
        const auto before = order.size();
        for (const auto file : files) {
            if (done(file)) continue;
            std::vector<SymbolId> cycle;
            for (const auto member : files) {
                if (!done(member) && together(file, member)) cycle.push_back(member);
            }
            const bool ready = std::all_of(cycle.begin(), cycle.end(), [&](SymbolId member) {
                return std::all_of(imports[member].begin(), imports[member].end(),
                                   [&](SymbolId dependency) { return done(dependency) || together(file, dependency); });
            });
            if (ready) {
                order.insert(order.end(), cycle.begin(), cycle.end());
                break;
            }
        }
        if (order.size() == before) {
            throw std::logic_error("no file is ready to initialize");
        }
    }

    Machine machine(program_, limits_, {}, {});
    std::vector<Outcome> outcomes;
    struct Item {
        const IrNode* node;
        SymbolId owner;
    };
    std::vector<Item> literals;
    std::vector<Item> reference_boxes;
    std::vector<Item> reference_values;
    std::vector<std::pair<SymbolId, std::vector<Item>>> steps;
    for (const auto file : order) {
        std::vector<Item> items;
        for (const auto& body : program_.bodies) {
            const auto kind = symbols.at(body.owner).kind;
            if ((kind != SymbolKind::File && kind != SymbolKind::Module) || file_of(body.owner) != file) {
                continue;
            }
            for (const auto& instruction : body.instructions) {
                Item item{&instruction, kind == SymbolKind::Module ? body.owner : no_symbol};
                if (instruction.op == IrOp::Initialize && literal(instruction.operands.front())) {
                    literals.push_back(item);
                } else {
                    switch (reference_preparation(instruction)) {
                    case ReferencePreparation::GlobalBox: reference_boxes.push_back(item); break;
                    case ReferencePreparation::GlobalValue: reference_values.push_back(item); break;
                    default: items.push_back(item); break;
                    }
                }
            }
        }
        std::stable_sort(items.begin(), items.end(),
                         [](const Item& a, const Item& b) { return a.node->span.offset < b.node->span.offset; });
        steps.emplace_back(file, std::move(items));
    }
    for (const auto& item : literals) {
        outcomes.push_back(machine.run_body({item.owner, {*item.node}}, item.owner));
    }
    for (const auto* preparations : {&reference_boxes, &reference_values})
        for (const auto& item : *preparations)
            outcomes.push_back(machine.run_body({item.owner, {*item.node}}, item.owner));
    for (const auto& [file, items] : steps) {
        for (const auto& item : items) {
            outcomes.push_back(machine.run_body({item.owner, {*item.node}}, item.owner));
            if (outcomes.back().status != Status::Returned) {
                break;
            }
        }
    }
    initial_globals_ = machine.globals();
    initial_heap_ = std::make_shared<const Heap>(machine.heap());
    return outcomes;
}

Outcome Evaluator::call(SymbolId function, const std::vector<Value>& arguments) {
    Machine machine(program_, limits_, initial_globals_, initial_heap_ ? *initial_heap_ : Heap{});
    return machine.call(function, arguments);
}

Outcome Evaluator::call_entry(SymbolId function) {
    Machine machine(program_, limits_, initial_globals_, initial_heap_ ? *initial_heap_ : Heap{});
    return machine.call_entry(function);
}

Outcome Evaluator::evaluate(const IrNode& expression) {
    // A closed expression cannot write globals, so one machine is reused.
    if (!closed_) {
        closed_ = std::make_unique<Machine>(program_, limits_, std::map<SymbolId, Value>{}, Heap{});
    }
    return closed_->evaluate_closed(expression);
}

} // namespace neura::testing
