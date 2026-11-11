#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace neura {

// The scheduler operations a trusted library function can be bound to with
// `scheduler("name")`, and that a SchedulerCall names in its text. The NVM
// scheduler implements each one (docs/runtime/NVR.md, "Scheduler
// operations"); scope entry, scope exit, and spawn have their own NeuraIR
// operations and are not listed here.
struct SchedulerOperation {
    std::string_view name;
    // The NVR scheduler operation code.
    std::uint8_t code;
    // One letter per argument: 'u' is uint64, 'i' is int64, 'b' is bool,
    // 'o' is an object the scheduler opened or opens as a channel, timer, or
    // guard, 'v' is any value type, 'f' is a function value of no arguments
    // created where the call is written, 'l' is a list of uint64, and 'a' is
    // an array.
    std::string_view arguments;
    // The result, with the same letters and 'c' for a uint32 outcome code,
    // or '0' for void.
    char result;
    // The operation takes one type argument, which the image carries as the
    // exit's target: a channel's element type.
    bool typed = false;
};

inline constexpr std::array scheduler_operations{
    SchedulerOperation{"join", 3, "u", 'v'},
    SchedulerOperation{"is_finished", 4, "u", 'b'},
    SchedulerOperation{"scope.cancel", 5, "u", '0'},
    SchedulerOperation{"scope.is_cancelled", 6, "u", 'b'},
    SchedulerOperation{"scope.set_deadline", 7, "ui", '0'},
    SchedulerOperation{"scope.deadline", 8, "u", 'i'},
    SchedulerOperation{"task.cancelled", 9, "", 'b'},
    SchedulerOperation{"task.deadline", 10, "", 'i'},
    SchedulerOperation{"take", 11, "", 'v'},
    SchedulerOperation{"channel.open", 12, "oi", '0', true},
    SchedulerOperation{"channel.send", 13, "ov", 'c'},
    SchedulerOperation{"channel.receive", 14, "o", 'c'},
    SchedulerOperation{"channel.try_send", 15, "ov", 'c'},
    SchedulerOperation{"channel.try_receive", 16, "o", 'c'},
    SchedulerOperation{"channel.close", 17, "o", '0'},
    SchedulerOperation{"timer.open", 18, "oi", '0'},
    SchedulerOperation{"timer.wait", 19, "o", 'c'},
    SchedulerOperation{"guard.open", 21, "ov", '0'},
    SchedulerOperation{"guard.enter", 22, "o", 'v'},
    SchedulerOperation{"guard.exit", 23, "o", '0'},
    // The first argument is a function value of no arguments written at the
    // call: the item runs its body in isolation. Then the item's step and
    // heap-cell limits.
    SchedulerOperation{"parallel.start", 24, "fii", 'u'},
    // Finishes the items a list names, in its order, and yields the array of
    // their results.
    SchedulerOperation{"parallel.finish", 25, "l", 'a'},
    // Checked by the compiler and absent from the image: the argument's
    // type must be transferable (Concurrency.md section 5.1). The value
    // passes through unchanged.
    SchedulerOperation{"transfer.check", 255, "v", 'v'},
};

// The code of an operation that exists only at compile time.
inline constexpr std::uint8_t compile_time_operation = 255;

// The select operation. Lowering builds its SchedulerCall from a select
// statement; no library function is bound to it. Its operands are the arms in
// source order, each a uint32 kind followed by that kind's operands, and its
// text ends with "+default" when a default arm follows them.
inline constexpr std::string_view select_operation = "select";
inline constexpr std::string_view select_with_default = "select+default";
inline constexpr std::uint8_t select_code = 20;
// Arm kinds, as in the image: receive(object), send(object, value),
// join(task), timer wait(object).
inline constexpr std::uint32_t select_receive = 0;
inline constexpr std::uint32_t select_send = 1;
inline constexpr std::uint32_t select_join = 2;
inline constexpr std::uint32_t select_timer = 3;

inline std::optional<SchedulerOperation> scheduler_operation(std::string_view name) {
    for (const auto& operation : scheduler_operations) {
        if (operation.name == name) return operation;
    }
    return std::nullopt;
}

} // namespace neura
