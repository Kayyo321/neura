#pragma once

#include "util/diagnostic.hpp"

#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace neura {

// Where a source file came from. Diagnostics treat library and generated
// files differently: library warnings are hidden by default, and `check --fix`
// edits only user files.
enum class SourceKind { User, Library, Generated };

// The text of one source file with the byte offset at which each line starts.
// Lines end at "\n", "\r\n", or "\r", as the lexer counts them.
class SourceFile {
public:
    SourceFile(std::string name, std::string text, SourceKind kind, std::string origin);

    const std::string& name() const { return name_; }
    const std::string& text() const { return text_; }
    SourceKind kind() const { return kind_; }
    // For a generated file, what it was generated from (a C header).
    const std::string& origin() const { return origin_; }

    std::size_t line_count() const { return line_starts_.size(); }
    // The zero-based line that contains byte `offset`; an offset at or past
    // the end is on the last line.
    std::size_t line_of(std::size_t offset) const;
    // The text of zero-based line `line`, without its line terminator.
    std::string_view line_text(std::size_t line) const;
    std::size_t line_start(std::size_t line) const { return line_starts_.at(line); }

private:
    std::string name_;
    std::string text_;
    SourceKind kind_;
    std::string origin_;
    std::vector<std::size_t> line_starts_;
};

// Every source file read during one compilation, keyed by the name that the
// lexer records in Span::file. The command line uses it to render source
// snippets under diagnostics.
class SourceMap {
public:
    // The map that the source loader registers files in.
    static SourceMap& global();

    // Adds or replaces the file named `name`.
    void add(std::string name, std::string text, SourceKind kind, std::string origin = {});
    // The file named `name`, or null.
    std::shared_ptr<const SourceFile> find(std::string_view name) const;
    void clear();
    std::vector<std::shared_ptr<const SourceFile>> files() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<const SourceFile>, std::less<>> files_;
};

// Redirect legacy compiler source helpers into a compilation-session map.
class SourceMapScope {
public:
    explicit SourceMapScope(SourceMap& map);
    ~SourceMapScope();
    SourceMapScope(const SourceMapScope&) = delete;
    SourceMapScope& operator=(const SourceMapScope&) = delete;
private:
    SourceMap* previous_;
};

// Helpers for building suggestions from the files in SourceMap::global().
// They find nothing for a file that is not registered (such as a test that
// lowers source it never registered), and callers then omit the suggestion.

// The span of `length` bytes at byte `offset` of `file`, with its line and
// column.
std::optional<Span> source_span(std::string_view file, std::size_t offset, std::size_t length);
// The text that `span` covers.
std::optional<std::string> source_text(const Span& span);
// Whether `file` is a registered file that the user wrote (not the standard
// library or a generated C import), so that an edit to it may be suggested.
bool user_source(std::string_view file);

} // namespace neura
