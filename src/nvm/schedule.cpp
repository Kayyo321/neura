#include "schedule.hpp"

#include <charconv>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace neura::nvm {

// SplitMix64: a fixed, portable generator, so a seed names the same
// schedule on every machine.
std::uint64_t ExploringChooser::next() {
    state_ += 0x9e3779b97f4a7c15ULL;
    auto z = state_;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

std::size_t ExploringChooser::choose(std::uint64_t tick, std::size_t runnable) {
    if (runnable <= 1) return 0;
    ++offered_;
    // Rejection sampling keeps the choice uniform for every queue length.
    const auto bound = static_cast<std::uint64_t>(runnable);
    const auto limit = ~std::uint64_t{0} - (~std::uint64_t{0} % bound);
    auto value = next();
    while (value >= limit) value = next();
    const auto position = value % bound;
    if (position != 0) choices_.push_back({tick, position});
    return static_cast<std::size_t>(position);
}

RecordedChooser::RecordedChooser(const std::vector<ScheduleChoice>& choices) {
    for (const auto& choice : choices) positions_.emplace(choice.tick, choice.position);
}

std::size_t RecordedChooser::choose(std::uint64_t tick, std::size_t runnable) {
    const auto found = positions_.find(tick);
    if (found == positions_.end()) return 0;
    if (found->second >= runnable) {
        if (!diverged_) {
            diverged_ = true;
            diverged_tick_ = tick;
        }
        return 0;
    }
    return static_cast<std::size_t>(found->second);
}

void write_schedule(const std::filesystem::path& path, const SavedSchedule& schedule) {
    if (schedule.test.empty() || schedule.test.find_first_of("\r\n") != std::string::npos) {
        throw std::runtime_error("a saved schedule needs a test name on one line");
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "neura-schedule 1\n"
        << "test " << schedule.test << '\n'
        << "seed " << schedule.seed << '\n'
        << "quantum " << schedule.quantum << '\n'
        << "virtual-time " << (schedule.virtual_time ? "on" : "off") << '\n';
    for (const auto& choice : schedule.choices) out << "choose " << choice.tick << ' ' << choice.position << '\n';
    out.flush();
    if (!out) throw std::runtime_error("could not write the schedule " + path.string());
}

SavedSchedule read_schedule(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("could not read the schedule " + path.string());
    std::size_t number = 0;
    const auto wrong = [&](const std::string& reason) -> std::runtime_error {
        return std::runtime_error(path.string() + ":" + std::to_string(number) + ": " + reason);
    };
    const auto integer = [&](std::string_view text) {
        std::uint64_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (text.empty() || error != std::errc{} || end != text.data() + text.size()) {
            throw wrong("expected an unsigned integer");
        }
        return value;
    };
    // Each header line is its keyword, one space, and its value.
    std::string line;
    const auto header = [&](std::string_view keyword) {
        ++number;
        if (!std::getline(in, line) || !std::string_view(line).starts_with(keyword) ||
            line.size() <= keyword.size() || line[keyword.size()] != ' ') {
            throw wrong("expected a '" + std::string(keyword) + "' line");
        }
        return std::string_view(line).substr(keyword.size() + 1);
    };
    SavedSchedule schedule;
    if (header("neura-schedule") != "1") throw wrong("unsupported schedule version; expected 1");
    schedule.test = std::string(header("test"));
    if (schedule.test.empty()) throw wrong("expected a test name");
    schedule.seed = integer(header("seed"));
    schedule.quantum = integer(header("quantum"));
    if (schedule.quantum == 0) throw wrong("the quantum must be positive");
    const auto time = header("virtual-time");
    if (time != "on" && time != "off") throw wrong("expected virtual-time on or off");
    schedule.virtual_time = time == "on";
    while (std::getline(in, line)) {
        ++number;
        const std::string_view view(line);
        const auto space = view.find(' ', 7);
        if (!view.starts_with("choose ") || space == std::string_view::npos) {
            throw wrong("expected 'choose <tick> <position>'");
        }
        const ScheduleChoice choice{integer(view.substr(7, space - 7)), integer(view.substr(space + 1))};
        if (choice.position == 0) throw wrong("a recorded choice is never the first runnable task");
        if (!schedule.choices.empty() && schedule.choices.back().tick >= choice.tick) {
            throw wrong("choices must be in ascending order of tick");
        }
        schedule.choices.push_back(choice);
    }
    return schedule;
}

} // namespace neura::nvm
