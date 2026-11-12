#include "source_map.hpp"

#include <algorithm>

namespace neura {
namespace { thread_local SourceMap* active_source_map = nullptr; }

SourceMapScope::SourceMapScope(SourceMap& map) : previous_(active_source_map) { active_source_map = &map; }
SourceMapScope::~SourceMapScope() { active_source_map = previous_; }

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
    return active_source_map ? *active_source_map : map;
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

std::vector<std::shared_ptr<const SourceFile>> SourceMap::files() const {
    const std::lock_guard lock(mutex_);
    std::vector<std::shared_ptr<const SourceFile>> result;
    for (const auto& [name, file] : files_) result.push_back(file);
    return result;
}

void SourceMap::clear() {
    const std::lock_guard lock(mutex_);
    files_.clear();
}

std::optional<Span> source_span(std::string_view file, std::size_t offset, std::size_t length) {
    const auto source = SourceMap::global().find(file);
    if (!source || offset > source->text().size() || length > source->text().size() - offset) return std::nullopt;
    const auto line = source->line_of(offset);
    std::size_t column = 1;
    const auto& text = source->text();
    for (auto i = source->line_start(line); i < offset; ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++column;
    }
    return Span{std::string(file), line + 1, column, offset, length};
}

std::optional<std::string> source_text(const Span& span) {
    const auto source = SourceMap::global().find(span.file);
    if (!source || span.offset > source->text().size() || span.length > source->text().size() - span.offset)
        return std::nullopt;
    return source->text().substr(span.offset, span.length);
}

bool user_source(std::string_view file) {
    const auto source = SourceMap::global().find(file);
    return source && source->kind() == SourceKind::User;
}

} // namespace neura
