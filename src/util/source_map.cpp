#include "source_map.hpp"

#include <algorithm>

namespace neura {

SourceFile::SourceFile(std::string name, std::string text, SourceKind kind, std::string origin)
    : name_(std::move(name)), text_(std::move(text)), kind_(kind), origin_(std::move(origin)) {
    line_starts_.push_back(0);
    for (std::size_t i = 0; i < text_.size(); ++i) {
        const char c = text_[i];
        if (c == '\r' && i + 1 < text_.size() && text_[i + 1] == '\n') ++i;
        if (c == '\r' || c == '\n') line_starts_.push_back(i + 1);
    }
}

std::size_t SourceFile::line_of(std::size_t offset) const {
    const auto next = std::upper_bound(line_starts_.begin(), line_starts_.end(), offset);
    return static_cast<std::size_t>(next - line_starts_.begin()) - 1;
}

std::string_view SourceFile::line_text(std::size_t line) const {
    const auto start = line_starts_.at(line);
    auto end = line + 1 < line_starts_.size() ? line_starts_[line + 1] : text_.size();
    while (end > start && (text_[end - 1] == '\n' || text_[end - 1] == '\r')) --end;
    return std::string_view(text_).substr(start, end - start);
}

SourceMap& SourceMap::global() {
    static SourceMap map;
    return map;
}

void SourceMap::add(std::string name, std::string text, SourceKind kind, std::string origin) {
    auto file = std::make_shared<const SourceFile>(name, std::move(text), kind, std::move(origin));
    const std::lock_guard lock(mutex_);
    files_.insert_or_assign(std::move(name), std::move(file));
}

std::shared_ptr<const SourceFile> SourceMap::find(std::string_view name) const {
    const std::lock_guard lock(mutex_);
    const auto found = files_.find(name);
    return found == files_.end() ? nullptr : found->second;
}

void SourceMap::clear() {
    const std::lock_guard lock(mutex_);
    files_.clear();
}

} // namespace neura
