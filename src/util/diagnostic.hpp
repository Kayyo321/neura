#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace neura {

struct Span {
    std::string file;
    std::size_t line{1};
    std::size_t column{1};
    std::size_t offset{};
    std::size_t length{};
};

enum class Severity { Error, Warning, Note, Help };

// How safely a tool may apply a suggestion without a person reading it
// (the same scale as rustc's).
enum class Applicability { MachineApplicable, MaybeIncorrect, HasPlaceholders, Unspecified };

struct DiagnosticLabel {
    Span span;
    std::string text;
    bool primary = false;
};

// Replaces the bytes of `span` (offset and length in its file) with
// `replacement`. A zero-length span is an insertion.
struct DiagnosticEdit {
    Span span;
    std::string replacement;
};

struct DiagnosticSuggestion {
    std::string message;
    std::vector<DiagnosticEdit> edits;
    Applicability applicability = Applicability::Unspecified;
};

// Everything a diagnostic carries beyond its primary span and message. A
// diagnostic thrown as Diagnostic(span, message) has none of it, so that
// construction stays as cheap as before for the speculative catches in the
// parser and in overload resolution.
struct DiagnosticData {
    std::string code;
    Severity severity = Severity::Error;
    // A warning's name for -W<name> and -Wno-<name>.
    std::string name;
    // The text under the primary span.
    std::string primary_label;
    // Secondary labels; the primary label is the diagnostic's span.
    std::vector<DiagnosticLabel> labels;
    std::vector<std::string> notes;
    std::vector<std::string> helps;
    std::vector<DiagnosticSuggestion> suggestions;
    // Structured facts for programmatic consumers, such as the kind and name
    // of an unresolved reference. Never rendered.
    std::map<std::string, std::string, std::less<>> args;
};

class Diagnostic : public std::runtime_error {
public:
    Diagnostic(Span location, std::string message)
        : std::runtime_error(std::move(message)), span(std::move(location)) {}

    static Diagnostic warning(Span location, std::string name, std::string message) {
        Diagnostic result(std::move(location), std::move(message));
        result.edit().severity = Severity::Warning;
        result.edit().name = std::move(name);
        return result;
    }

    Span span;

    // Fluent helpers. Each returns the diagnostic so that a site reads
    // throw Diagnostic(span, "...").with_code("E0201").label("...");
    Diagnostic& with_code(std::string code) & { edit().code = std::move(code); return *this; }
    Diagnostic&& with_code(std::string code) && { return std::move(with_code(std::move(code))); }
    // The text under the primary span.
    Diagnostic& label(std::string text) & { edit().primary_label = std::move(text); return *this; }
    Diagnostic&& label(std::string text) && { return std::move(label(std::move(text))); }
    // A secondary label, which may be in another file.
    Diagnostic& label(Span at, std::string text) & {
        edit().labels.push_back({std::move(at), std::move(text), false});
        return *this;
    }
    Diagnostic&& label(Span at, std::string text) && { return std::move(label(std::move(at), std::move(text))); }
    Diagnostic& note(std::string text) & { edit().notes.push_back(std::move(text)); return *this; }
    Diagnostic&& note(std::string text) && { return std::move(note(std::move(text))); }
    Diagnostic& help(std::string text) & { edit().helps.push_back(std::move(text)); return *this; }
    Diagnostic&& help(std::string text) && { return std::move(help(std::move(text))); }
    Diagnostic& suggest(std::string message, std::vector<DiagnosticEdit> edits, Applicability applicability) & {
        edit().suggestions.push_back({std::move(message), std::move(edits), applicability});
        return *this;
    }
    Diagnostic&& suggest(std::string message, std::vector<DiagnosticEdit> edits, Applicability applicability) && {
        return std::move(suggest(std::move(message), std::move(edits), applicability));
    }
    Diagnostic& suggest(std::string message, Span at, std::string replacement, Applicability applicability) & {
        return suggest(std::move(message), {{std::move(at), std::move(replacement)}}, applicability);
    }
    Diagnostic&& suggest(std::string message, Span at, std::string replacement, Applicability applicability) && {
        return std::move(suggest(std::move(message), std::move(at), std::move(replacement), applicability));
    }
    Diagnostic& arg(std::string key, std::string value) & {
        edit().args.insert_or_assign(std::move(key), std::move(value));
        return *this;
    }
    Diagnostic&& arg(std::string key, std::string value) && {
        return std::move(arg(std::move(key), std::move(value)));
    }

    // The extra data, or null for a plain diagnostic.
    const DiagnosticData* data() const { return data_.get(); }
    std::string_view code() const { return data_ ? std::string_view(data_->code) : std::string_view{}; }
    Severity severity() const { return data_ ? data_->severity : Severity::Error; }
    std::string_view name() const { return data_ ? std::string_view(data_->name) : std::string_view{}; }
    // A structured argument, or null.
    const std::string* argument(std::string_view key) const {
        if (!data_) return nullptr;
        const auto found = data_->args.find(key);
        return found == data_->args.end() ? nullptr : &found->second;
    }
    // The primary label first, then the secondary labels in order.
    std::vector<DiagnosticLabel> labels() const {
        std::vector<DiagnosticLabel> result{{span, data_ ? data_->primary_label : std::string{}, true}};
        if (data_) result.insert(result.end(), data_->labels.begin(), data_->labels.end());
        return result;
    }

    // A copy with `message` in place of this one's, keeping span and data.
    Diagnostic with_message(std::string message) const {
        Diagnostic result(span, std::move(message));
        result.data_ = data_;
        return result;
    }

    // Mutable data, created on first use. Copies share the data until one of
    // them is edited (copy on write), so that adding a note to a copy of a
    // caught diagnostic leaves the original unchanged.
    DiagnosticData& edit() {
        if (!data_) data_ = std::make_shared<DiagnosticData>();
        else if (data_.use_count() > 1) data_ = std::make_shared<DiagnosticData>(*data_);
        return *data_;
    }

private:
    std::shared_ptr<DiagnosticData> data_;
};

// Collects warnings while the pipeline runs. Errors still throw; a warning is
// appended here and printed by the command line before an error or at a
// successful end. Stages report through the sink installed on this thread,
// so a library that installs none (the C++ tests) discards warnings.
class DiagnosticSink {
public:
    void report(Diagnostic diagnostic) { entries_.push_back(std::move(diagnostic)); }
    const std::vector<Diagnostic>& entries() const { return entries_; }
    std::size_t size() const { return entries_.size(); }
    void truncate(std::size_t size) {
        if (size < entries_.size()) entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(size), entries_.end());
    }
    void clear() { entries_.clear(); }

    // The sink installed on this thread, or null.
    static DiagnosticSink*& current() {
        thread_local DiagnosticSink* sink = nullptr;
        return sink;
    }

private:
    std::vector<Diagnostic> entries_;
};

// Reports a warning to the installed sink, if any.
inline void report_warning(Diagnostic diagnostic) {
    if (auto* sink = DiagnosticSink::current()) sink->report(std::move(diagnostic));
}

// Installs `sink` for this thread for the guard's lifetime.
class DiagnosticSinkScope {
public:
    explicit DiagnosticSinkScope(DiagnosticSink& sink) : previous_(std::exchange(DiagnosticSink::current(), &sink)) {}
    ~DiagnosticSinkScope() { DiagnosticSink::current() = previous_; }
    DiagnosticSinkScope(const DiagnosticSinkScope&) = delete;
    DiagnosticSinkScope& operator=(const DiagnosticSinkScope&) = delete;

private:
    DiagnosticSink* previous_;
};

// Discards every warning reported while the guard is alive, unless commit()
// is called. Speculative work (trying a parse, or lowering an argument to
// rank an overload) holds one, so that warnings from a path that is
// abandoned or lowered again are not reported twice or at all.
class SpeculativeDiagnostics {
public:
    SpeculativeDiagnostics()
        : sink_(DiagnosticSink::current()), mark_(sink_ ? sink_->size() : 0) {}
    ~SpeculativeDiagnostics() {
        if (sink_ && !committed_) sink_->truncate(mark_);
    }
    void commit() { committed_ = true; }
    SpeculativeDiagnostics(const SpeculativeDiagnostics&) = delete;
    SpeculativeDiagnostics& operator=(const SpeculativeDiagnostics&) = delete;

private:
    DiagnosticSink* sink_;
    std::size_t mark_;
    bool committed_ = false;
};

} // namespace neura
