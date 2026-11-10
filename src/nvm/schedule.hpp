#pragma once

#include "machine.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace neura::nvm {

// One scheduling choice that differs from the reference scheduler's: at
// decision `tick`, the task at `position` of the runnable queue ran instead
// of the first (docs/lang/Concurrency.md section 14.4).
struct ScheduleChoice {
    std::uint64_t tick = 0;
    std::uint64_t position = 0;

    bool operator==(const ScheduleChoice&) const = default;
};

// Explores schedules: picks uniformly among the runnable tasks from a seed
// and keeps every choice that was not the first, so the schedule it ran can
// be saved and run again. The same seed gives the same choices for the same
// run. Every schedule it produces is legal: only the order among runnable
// tasks varies.
class ExploringChooser final : public ScheduleChooser {
public:
    explicit ExploringChooser(std::uint64_t seed) : state_(seed) {}

    std::size_t choose(std::uint64_t tick, std::size_t runnable) override;
    const std::vector<ScheduleChoice>& choices() const { return choices_; }
    // The decisions at which more than one task could run. Zero means the
    // run has a single legal schedule.
    std::uint64_t offered() const { return offered_; }

private:
    std::uint64_t next();

    std::uint64_t state_;
    std::uint64_t offered_ = 0;
    std::vector<ScheduleChoice> choices_;
};

// Reproduces a saved schedule: the recorded position at each recorded tick,
// and the first runnable task everywhere else.
class RecordedChooser final : public ScheduleChooser {
public:
    explicit RecordedChooser(const std::vector<ScheduleChoice>& choices);

    std::size_t choose(std::uint64_t tick, std::size_t runnable) override;
    // True when a recorded position did not exist in the run: the schedule
    // belongs to a different run.
    bool diverged() const { return diverged_; }
    std::uint64_t diverged_tick() const { return diverged_tick_; }

private:
    std::map<std::uint64_t, std::uint64_t> positions_;
    bool diverged_ = false;
    std::uint64_t diverged_tick_ = 0;
};

// A clock that moves only when the run asks it to: `--virtual-time`. Time
// stands still while any task can run and jumps to the earliest timer when
// every task is parked, so a two-second timeout takes no real time and is
// exact. It starts at zero.
class VirtualTaskClock final : public TaskClock {
public:
    std::int64_t now() override { return now_; }
    void wait_until(std::int64_t deadline) override {
        if (deadline > now_) now_ = deadline;
    }
    bool virtual_time() const override { return true; }

private:
    std::int64_t now_ = 0;
};

// A schedule that `neura test --save-schedule` wrote: the test it belongs
// to, the settings that decide where the scheduling decisions fall, and
// every choice that differed from the reference scheduler's.
struct SavedSchedule {
    std::string test;
    std::uint64_t seed = 0;
    std::uint64_t quantum = 0;
    bool virtual_time = false;
    std::vector<ScheduleChoice> choices;
};

// The file is text: a "neura-schedule 1" line, then "test", "seed",
// "quantum", and "virtual-time" lines, then one "choose <tick> <position>"
// line for each choice, in ascending order of tick. read_schedule throws
// std::runtime_error naming the line that is wrong.
void write_schedule(const std::filesystem::path& path, const SavedSchedule& schedule);
SavedSchedule read_schedule(const std::filesystem::path& path);

} // namespace neura::nvm
