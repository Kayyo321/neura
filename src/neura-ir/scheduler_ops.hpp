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
    // One letter per argument: 'u' is uint64, 'i' is int64, 'b' is bool, and
    // 'v' is any value type.
    std::string_view arguments;
    // The result, with the same letters, or '0' for void.
    char result;
};

inline constexpr std::array scheduler_operations{
    SchedulerOperation{"join", 3, "u", 'v'},
    SchedulerOperation{"is_finished", 4, "u", 'b'},
};

inline std::optional<SchedulerOperation> scheduler_operation(std::string_view name) {
    for (const auto& operation : scheduler_operations) {
        if (operation.name == name) return operation;
    }
    return std::nullopt;
}

} // namespace neura
