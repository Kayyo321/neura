#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

// The edit distance between `a` and `b`, counting bytes: insertions,
// deletions, substitutions, and swaps of adjacent characters each cost 1
// (the optimal string alignment distance), so "cuont" is 1 from "count".
inline std::size_t edit_distance(std::string_view a, std::string_view b) {
    const auto width = b.size() + 1;
    std::vector<std::size_t> table((a.size() + 1) * width);
    const auto at = [&](std::size_t i, std::size_t j) -> std::size_t& { return table[i * width + j]; };
    for (std::size_t i = 0; i <= a.size(); ++i) at(i, 0) = i;
    for (std::size_t j = 0; j <= b.size(); ++j) at(0, j) = j;
    for (std::size_t i = 1; i <= a.size(); ++i) {
        for (std::size_t j = 1; j <= b.size(); ++j) {
            auto best = std::min({at(i - 1, j) + 1, at(i, j - 1) + 1, at(i - 1, j - 1) + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) best = std::min(best, at(i - 2, j - 2) + 1);
            at(i, j) = best;
        }
    }
    return at(a.size(), b.size());
}

// The candidate closest to `name` for a "did you mean" suggestion: within an
// edit distance of a third of the name's length (at least 1), or equal to it
// ignoring ASCII case. Ties go to the earliest candidate, so callers pass
// candidates in a stable order. Empty when nothing is close or when `name`
// itself is a candidate.
inline std::optional<std::string> closest_name(std::string_view name, const std::vector<std::string>& candidates) {
    const auto lower = [](std::string_view text) {
        std::string result(text);
        for (auto& c : result) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        return result;
    };
    const auto folded = lower(name);
    const std::size_t limit = std::max<std::size_t>(1, name.size() / 3);
    std::optional<std::string> best;
    std::size_t best_distance = limit + 1;
    for (const auto& candidate : candidates) {
        if (candidate == name) return std::nullopt;
        if (candidate.empty() || candidate.front() == '$') continue;
        auto distance = lower(candidate) == folded ? 0 : edit_distance(name, candidate);
        if (distance < best_distance) {
            best_distance = distance;
            best = candidate;
        }
    }
    return best;
}

} // namespace neura
