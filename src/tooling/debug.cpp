#include "debug.hpp"

#include "json.hpp"
#include "protocol.hpp"
#include "test_protocol.hpp"
#include "backend/converter.hpp"
#include "cli/source_loader.hpp"
#include "neura-ir/reference_storage.hpp"
#include "neura-ir/specialize.hpp"
#include "neura-ir/verifier.hpp"
#include "nvm/driver.hpp"
#include "nvm/machine.hpp"
#include "nvm/providers.hpp"
#include "nvr/format.hpp"
#include "nvr/names.hpp"
#include "runner/runner.hpp"
#include "util/sha256.hpp"
#include "util/source_map.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <unistd.h>
#endif

namespace neura::tooling {
namespace {
namespace fs = std::filesystem;

std::string path_key(const fs::path& path, const fs::path& base = fs::current_path()) {
    std::error_code error;
    auto absolute = path.is_absolute() ? path : base / path;
    auto result = fs::weakly_canonical(absolute, error);
    return (error ? absolute.lexically_normal() : result).generic_string();
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), {}};
}

// SMAP columns count Unicode scalar values. DAP columns count UTF-16 code
// units, as the editor does. CRLF and other line endings are handled by the
// same SourceFile implementation as the compiler.
std::size_t utf16_column(std::string_view line, std::size_t scalar_column) {
    std::size_t scalars = 1, units = 1;
    for (std::size_t i = 0; i < line.size() && scalars < scalar_column; ++scalars) {
        const auto c = static_cast<unsigned char>(line[i]);
        const auto count = c < 0x80 ? 1U : c < 0xe0 ? 2U : c < 0xf0 ? 3U : 4U;
        units += count == 4 ? 2 : 1;
        i += std::min<std::size_t>(count, line.size() - i);
    }
    return units;
}

struct Source {
    std::string path;
    std::shared_ptr<const SourceFile> snapshot;
    int reference = 0;
    std::string checksum;
};

struct Breakpoint {
    int id = 0;
    int line = 0;
    int column = 1;
    bool verified = false;
    std::string path;
    std::string condition;
    std::string hit_condition;
    std::string log_message;
    std::uint64_t hits = 0;
    std::string message;
};

class Adapter;

class EventSink final : public nvm::ConsoleSink {
public:
    EventSink(Adapter& adapter, std::string category) : adapter_(adapter), category_(std::move(category)) {}
    bool write(std::string_view bytes) override;
private:
    Adapter& adapter_;
    std::string category_;
};

class ProgramInput {
public:
    ProgramInput() {
#ifndef _WIN32
        int descriptors[2];
        if (::pipe(descriptors) != 0) throw std::runtime_error("cannot open debuggee input pipe");
        read_ = descriptors[0]; write_ = descriptors[1];
        ::fcntl(read_, F_SETFD, FD_CLOEXEC);
        ::fcntl(write_, F_SETFD, FD_CLOEXEC);
        ::fcntl(write_, F_SETFL, ::fcntl(write_, F_GETFL) | O_NONBLOCK);
#endif
    }
    ~ProgramInput() {
        close_input();
#ifndef _WIN32
        if (read_ >= 0) ::close(read_);
#endif
    }
    int descriptor() const { return read_; }
    void write(std::string_view bytes) {
#ifndef _WIN32
        if (write_ < 0) throw std::runtime_error("program input is closed");
        // A bounded nonblocking write lets the protocol continue to process
        // pause/disconnect while a program has stopped consuming input.
        if (bytes.size() > 4096) throw std::runtime_error("send input in chunks of at most 4096 bytes");
        const auto written = ::write(write_, bytes.data(), bytes.size());
        if (written < 0 || static_cast<std::size_t>(written) != bytes.size())
            throw std::runtime_error("program input buffer is full");
#else
        (void)bytes;
        throw std::runtime_error("debug console input requires a POSIX workspace host");
#endif
    }
    void close_input() {
#ifndef _WIN32
        if (write_ >= 0) { ::close(write_); write_ = -1; }
#endif
    }
private:
    int read_ = -1;
    int write_ = -1;
};

class Adapter {
public:
    Adapter(std::istream& input, std::ostream& output, fs::path executable)
        : input_(input), output_(output), executable_(std::move(executable)) {}
    ~Adapter() { terminate(); }
    int run();
    void event(std::string_view name, Json body = Json::Object{}) {
        write_message(output_, Json{{"seq", sequence_.fetch_add(1)}, {"type", "event"},
                                    {"event", name}, {"body", std::move(body)}});
    }
private:
    struct FrameRef { std::size_t thread; std::size_t frame; };
    struct Variables {
        enum class Kind { Locals, Globals, Children } kind = Kind::Locals;
        FrameRef frame{};
        nvm::DebugValue value;
    };
    enum class Step { Continue, Into, Over, Out };

    std::istream& input_;
    std::ostream& output_;
    fs::path executable_;
    std::atomic<int> sequence_{1};
    std::mutex mutex_;
    std::condition_variable condition_;
    std::thread worker_;
    std::atomic<bool> terminate_{false};
    std::atomic<bool> pause_{false};
    bool launched_ = false;
    bool configured_ = false;
    bool stopped_ = false;
    bool stop_on_entry_ = false;
    std::atomic<bool> trap_stops_{true};
    Step step_ = Step::Continue;
    nvm::DebugPosition step_origin_;
    std::string step_location_;
    nvm::DebugPosition current_position_;
    nvm::DebugSnapshot snapshot_;
    std::optional<nvm::Trap> trap_;
    std::unique_ptr<nvm::Execution> execution_;
    std::unique_ptr<ProgramInput> program_input_;
    std::optional<nvr::Image> image_;
    std::optional<nvr::Names> names_;
    fs::path cwd_;
    std::map<std::string, Source> sources_;
    std::map<std::pair<std::uint32_t, std::uint32_t>, const nvr::SourceLocation*> locations_;
    std::map<std::string, std::vector<Breakpoint>> breakpoints_;
    int breakpoint_sequence_ = 1;
    int source_sequence_ = 1;
    int reference_sequence_ = 1;
    std::map<int, FrameRef> frames_;
    std::map<int, Variables> variables_;
    Json launch_arguments_;

    void response(const Json& request, Json body = Json::Object{}, std::string error = {}) {
        Json message{{"seq", sequence_.fetch_add(1)}, {"type", "response"},
                     {"request_seq", request["seq"]}, {"success", error.empty()},
                     {"command", request["command"]}};
        if (error.empty()) message["body"] = std::move(body);
        else message["message"] = std::move(error);
        write_message(output_, message);
    }
    void handle(const Json& request);
    void launch(const Json& arguments);
    void execute();
    void terminate();
    void stopped(std::string reason, const nvm::DebugPosition& position, std::string description = {},
                 std::vector<int> breakpoint_ids = {});
    bool should_stop(const nvm::DebugPosition& position);
    std::string location_key(const nvm::DebugPosition& position) const;
    const nvr::SourceLocation* location(std::uint32_t function, std::uint32_t block) const;
    Json source(const nvr::SourceLocation& location) const;
    std::string function_name(std::uint32_t function) const;
    std::string type_name(const nvm::DebugValue& value) const;
    Json variable(std::string name, const nvm::DebugValue& value);
    Json variables(const Json& arguments);
    Json evaluate(const Json& arguments);
    nvm::DebugValue expression(std::string_view text, const FrameRef& frame);
    FrameRef find_frame(const Json& arguments) const;
    Json set_breakpoints(const Json& arguments, bool preserve_ids = false);
    Json stack_trace(const Json& arguments);
    bool hit(Breakpoint& breakpoint, const FrameRef& frame);
    std::string interpolate(std::string_view text, const FrameRef& frame);
    void resume(Step step, std::uint64_t task);
    std::vector<std::string> array_strings(const Json& value) const {
        std::vector<std::string> result;
        for (const auto& item : value.items()) {
            if (!item.is_string()) throw std::runtime_error("expected a string array");
            result.push_back(item.string());
        }
        return result;
    }
};

bool EventSink::write(std::string_view bytes) {
    // Bound each protocol event even when an image emits a very large value.
    while (!bytes.empty()) {
        auto count = std::min<std::size_t>(bytes.size(), 16384);
        if (count < bytes.size()) {
            while (count != 0 && (static_cast<unsigned char>(bytes[count]) & 0xc0U) == 0x80U) --count;
            if (count == 0) count = std::min<std::size_t>(bytes.size(), 16384);
        }
        const auto encoded = test_output_json({}, category_, bytes.substr(0, count));
        adapter_.event("output", Json{{"category", category_}, {"output", encoded["output"]}});
        bytes.remove_prefix(count);
    }
    return true;
}

int Adapter::run() {
    for (;;) {
        std::optional<Json> message;
        try { message = read_message(input_); }
        catch (const std::exception& error) {
            event("output", Json{{"category", "stderr"}, {"output", std::string(error.what()) + "\n"}});
            terminate();
            return 1;
        }
        if (!message) break;
        const auto& request = *message;
        if (request["type"].string() != "request") continue;
        try { handle(request); }
        catch (const std::exception& error) { response(request, {}, error.what()); }
        if (request["command"].string() == "disconnect") break;
    }
    terminate();
    return 0;
}

const nvr::SourceLocation* Adapter::location(std::uint32_t function, std::uint32_t block) const {
    const auto found = locations_.find({function, block});
    return found == locations_.end() ? nullptr : found->second;
}

std::string Adapter::location_key(const nvm::DebugPosition& position) const {
    const auto* at = location(position.function, position.block);
    return at ? at->path + ":" + std::to_string(at->line) : std::string();
}

Json Adapter::source(const nvr::SourceLocation& at) const {
    const auto found = sources_.find(at.path);
    const auto path = found == sources_.end() ? path_key(at.path, cwd_) : found->second.path;
    Json result{{"name", fs::path(path).filename().string()}, {"path", path}};
    if (found != sources_.end() && found->second.snapshot) {
        result["sourceReference"] = found->second.reference;
        result["checksums"] = Json::Array{Json{{"algorithm", "SHA256"}, {"checksum", found->second.checksum}}};
    }
    return result;
}

std::string Adapter::function_name(std::uint32_t function) const {
    if (names_ && function < names_->functions.size() && !names_->functions[function].empty())
        return names_->functions[function];
    return "function " + std::to_string(function);
}

std::string Adapter::type_name(const nvm::DebugValue& value) const {
    if (value.layout != nvr::absent && names_ && value.layout < names_->layouts.size() &&
        !names_->layouts[value.layout].empty()) return names_->layouts[value.layout];
    if (!image_ || value.type >= image_->types.size()) return "value";
    const auto& type = image_->types[value.type];
    switch (type.kind) {
    case nvr::TypeKind::Void: return "void";
    case nvr::TypeKind::Bool: return "bool";
    case nvr::TypeKind::Unsigned: return "uint" + std::to_string(type.width);
    case nvr::TypeKind::Signed: return "int" + std::to_string(type.width);
    case nvr::TypeKind::Float: return "float" + std::to_string(type.width);
    case nvr::TypeKind::BFloat: return "bfloat16";
    case nvr::TypeKind::Text: return "string";
    case nvr::TypeKind::Bytes: return "bytes";
    case nvr::TypeKind::Array: return "array";
    case nvr::TypeKind::List: return "list";
    case nvr::TypeKind::Object: return "object";
    }
    return "value";
}

Json Adapter::variable(std::string name, const nvm::DebugValue& value) {
    int reference = 0;
    if (value.initialized && value.children != 0) {
        reference = reference_sequence_++;
        variables_.emplace(reference, Variables{Variables::Kind::Children, {}, value});
    }
    Json result{{"name", std::move(name)}, {"value", value.text}, {"type", type_name(value)},
                {"variablesReference", reference},
                {"presentationHint", Json{{"attributes", Json::Array{"readOnly"}}}}};
    if (reference != 0) {
        if (value.layout != nvr::absent) result["namedVariables"] = value.children;
        else result["indexedVariables"] = value.children;
    }
    return result;
}

void Adapter::launch(const Json& arguments) {
    if (launched_) throw std::runtime_error("a debug session can launch only once; start a new session to restart");
    cwd_ = arguments["cwd"].string(fs::current_path().string());
    cwd_ = path_key(cwd_);
    if (!fs::is_directory(cwd_)) throw std::runtime_error("debug working directory does not exist");
    fs::current_path(cwd_);
    for (const auto& [name, value] : arguments["env"].fields()) {
        if (name.empty() || name.find('=') != std::string::npos || name.find('\0') != std::string::npos)
            throw std::runtime_error("invalid environment variable name");
#ifndef _WIN32
        if (value.is_null()) ::unsetenv(name.c_str());
        else if (value.is_string() && value.string().find('\0') == std::string::npos) ::setenv(name.c_str(), value.string().c_str(), 1);
        else throw std::runtime_error("environment values must be strings or null");
#endif
    }
    auto files = array_strings(arguments["testFiles"]);
    if (files.empty()) files = array_strings(arguments["sourceFiles"]);
    if (files.empty()) files = array_strings(arguments["files"]);
    const auto program_path = arguments["program"].string();
    if (files.empty() && !program_path.empty()) files.push_back(program_path);
    if (files.empty()) throw std::runtime_error("launch needs program or sourceFiles");
    std::vector<fs::path> inputs;
    for (const auto& file : files) inputs.emplace_back(path_key(file, cwd_));
    std::vector<fs::path> roots;
    for (const auto& root : array_strings(arguments["sourceRoots"])) roots.emplace_back(path_key(root, cwd_));
    auto library = fs::path(arguments["standardLibrary"].string());
    if (library.empty()) {
        const auto prefix = fs::path(path_key(executable_)).parent_path().parent_path();
        for (const auto& candidate : {prefix / "share/neura/stdlib", prefix / "stdlib"}) {
            if (fs::is_directory(candidate / "std")) { library = candidate; break; }
        }
    }
    SourceMap source_map;
    const SourceMapScope map_scope(source_map);
    if (inputs.front().extension() == ".nvr") {
        if (inputs.size() != 1) throw std::runtime_error("an image launch takes exactly one .nvr file");
        image_ = nvr::load(runner::read_file(inputs.front()));
    } else {
        auto program = lower_sources(inputs, roots, library);
        if (!program.native_units.empty())
            throw std::runtime_error("source debugging of C imports needs a prebuilt .nvr image");
        for (const auto& file : source_map.files()) {
            const auto label = backend::source_label(program, file->name());
            sources_.emplace(label, Source{file->name(), file, source_sequence_++, util::sha256_hex(file->text())});
        }
        const auto test_filter = arguments["testFilter"].string(arguments["testId"].string());
        if (test_filter.empty()) {
            image_ = backend::build(std::move(program), backend::BuildOptions{false, true, false, true}).image;
        } else {
            const auto id_root = fs::path(arguments["workspaceRoot"].string(arguments["workspaceFolder"].string(cwd_.string())));
            const auto tests = discover_tests(program, inputs, id_root);
            const auto test = std::find_if(tests.begin(), tests.end(), [&](const auto& item) { return item.id == test_filter; });
            if (test == tests.end()) throw std::runtime_error("selected test no longer exists: " + test_filter);
            const auto symbol = test->symbol;
            require_valid_ir(program, "before debug specialization");
            materialize_reference_storage(program);
            specialize(program);
            require_valid_ir(program, "before debug conversion");
            backend::ConversionOptions conversion;
            conversion.entries = {symbol};
            conversion.lift_hints = false;
            conversion.debug_statements = true;
            image_ = backend::convert(program, conversion).image;
        }
        // Load through the regular verifier before running generated code.
        image_ = nvr::load(nvr::encode(*image_));
    }
    if (!image_->source_map || image_->source_map->empty())
        throw std::runtime_error("the image has no source map; rebuild without --no-source-map");
    names_ = nvr::decode_names(*image_);
    for (const auto& at : *image_->source_map) {
        locations_.emplace(std::pair{at.function, at.block}, &at);
        if (!sources_.contains(at.path)) {
            const auto resolved = path_key(at.path, arguments["sourceRoot"].string(cwd_.string()));
            sources_.emplace(at.path, Source{resolved, {}, 0, {}});
        }
    }
    program_input_ = std::make_unique<ProgramInput>();
    launch_arguments_ = arguments;
    stop_on_entry_ = arguments["stopOnEntry"].boolean(false);
    launched_ = true;
    // Breakpoints often arrive before launch. Revalidate them against the
    // compiled source map now that executable locations are known.
    for (auto& [path, points] : breakpoints_) {
        Json list = Json::Array{};
        for (const auto& point : points) list.push_back(Json{{"id", point.id}, {"line", point.line}, {"condition", point.condition},
            {"hitCondition", point.hit_condition}, {"logMessage", point.log_message}});
        const auto updated = set_breakpoints(Json{{"source", Json{{"path", path}}}, {"breakpoints", list}}, true);
        for (const auto& point : updated["breakpoints"].items()) event("breakpoint", Json{{"reason", "changed"}, {"breakpoint", point}});
    }
}

Json Adapter::set_breakpoints(const Json& arguments, bool preserve_ids) {
    const auto path = path_key(arguments["source"]["path"].string(), cwd_.empty() ? fs::current_path() : cwd_);
    std::vector<Breakpoint> points;
    Json result = Json::Array{};
    for (const auto& item : arguments["breakpoints"].items()) {
        Breakpoint point;
        point.id = preserve_ids ? static_cast<int>(item["id"].integer()) : breakpoint_sequence_++;
        point.path = path;
        point.line = static_cast<int>(item["line"].integer());
        point.condition = item["condition"].string();
        point.hit_condition = item["hitCondition"].string();
        point.log_message = item["logMessage"].string();
        if (point.line < 1) point.message = "breakpoint line must be positive";
        else if (!image_) point.message = "pending program launch";
        else {
            int best = std::numeric_limits<int>::max();
            const nvr::SourceLocation* chosen = nullptr;
            for (const auto& at : *image_->source_map) {
                const auto found = sources_.find(at.path);
                if (found == sources_.end() || found->second.path != path || at.line < static_cast<unsigned>(point.line)) continue;
                if (at.line < static_cast<unsigned>(best)) { best = static_cast<int>(at.line); chosen = &at; }
            }
            if (chosen) {
                point.verified = true;
                if (best != point.line) point.message = "moved to the next executable source location";
                point.line = best;
                point.column = static_cast<int>(chosen->column);
                if (const auto found = sources_.find(chosen->path); found != sources_.end() && found->second.snapshot) {
                    const auto line = found->second.snapshot->line_text(static_cast<std::size_t>(point.line - 1));
                    point.column = static_cast<int>(utf16_column(line, chosen->column));
                    std::error_code error;
                    if (fs::is_regular_file(path, error) && util::sha256_hex(read_text(path)) != found->second.checksum) {
                        point.verified = false;
                        point.message = "source changed since compilation; restart debugging to rebuild";
                    }
                }
            } else point.message = "no executable location at or after this line in the compiled program";
        }
        Json encoded{{"id", point.id}, {"verified", point.verified}, {"line", point.line}, {"column", point.column}};
        if (!point.message.empty()) encoded["message"] = point.message;
        result.push_back(std::move(encoded));
        points.push_back(std::move(point));
    }
    breakpoints_[path] = std::move(points);
    return Json{{"breakpoints", result}};
}

void Adapter::terminate() {
    terminate_.store(true);
    if (program_input_) program_input_->close_input();
    condition_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void Adapter::resume(Step step, std::uint64_t task) {
    if (!launched_) throw std::runtime_error("no program has been launched");
    if (!stopped_) throw std::runtime_error("the program is not stopped");
    step_ = step;
    step_origin_ = current_position_;
    if (step != Step::Continue && task != current_position_.task) {
        const auto thread = std::find_if(snapshot_.threads.begin(), snapshot_.threads.end(),
                                         [&](const auto& value) { return value.id == task; });
        if (thread == snapshot_.threads.end() || thread->frames.empty())
            throw std::runtime_error("the selected task has no active frame");
        const auto& frame = thread->frames.front();
        step_origin_ = nvm::DebugPosition{task, frame.function, frame.block, thread->frames.size()};
    }
    step_location_ = location_key(step_origin_);
    stopped_ = false;
    frames_.clear(); variables_.clear();
    condition_.notify_all();
}

void Adapter::stopped(std::string reason, const nvm::DebugPosition& position,
                      std::string description, std::vector<int> breakpoint_ids) {
    std::unique_lock lock(mutex_);
    if (terminate_.load()) return;
    current_position_ = position;
    snapshot_ = execution_->debug_snapshot();
    stopped_ = true;
    frames_.clear(); variables_.clear();
    Json body{{"reason", std::move(reason)}, {"threadId", position.task + 1}, {"allThreadsStopped", true}};
    if (!description.empty()) body["description"] = std::move(description);
    if (!breakpoint_ids.empty()) {
        Json ids = Json::Array{};
        for (int id : breakpoint_ids) ids.push_back(id);
        body["hitBreakpointIds"] = std::move(ids);
    }
    event("stopped", std::move(body));
    condition_.wait(lock, [&] { return !stopped_ || terminate_.load(); });
}

bool Adapter::hit(Breakpoint& point, const FrameRef& frame) {
    ++point.hits;
    if (!point.hit_condition.empty()) {
        std::string_view text(point.hit_condition);
        std::string_view op = "==";
        for (auto candidate : {std::string_view(">="), std::string_view("<="), std::string_view("=="), std::string_view(">"), std::string_view("<"), std::string_view("%")}) {
            if (text.starts_with(candidate)) { op = candidate; text.remove_prefix(candidate.size()); break; }
        }
        while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
        std::uint64_t count = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
        if (error != std::errc{} || end != text.data() + text.size() || count == 0)
            throw std::runtime_error("hit condition must be a positive count, >= N, > N, <= N, < N, or % N");
        const bool matches = op == ">=" ? point.hits >= count : op == ">" ? point.hits > count :
                             op == "<=" ? point.hits <= count : op == "<" ? point.hits < count :
                             op == "%" ? point.hits % count == 0 : point.hits == count;
        if (!matches) return false;
    }
    if (!point.condition.empty()) {
        const auto value = expression(point.condition, frame);
        if (value.text != "true" && value.text != "false") throw std::runtime_error("breakpoint condition must be Boolean");
        if (value.text == "false") return false;
    }
    return true;
}

std::string Adapter::interpolate(std::string_view text, const FrameRef& frame) {
    std::string result;
    while (!text.empty()) {
        const auto open = text.find('{');
        if (open == std::string_view::npos) { result += text; break; }
        result += text.substr(0, open);
        text.remove_prefix(open + 1);
        const auto close = text.find('}');
        if (close == std::string_view::npos) { result += '{'; result += text; break; }
        try { result += expression(text.substr(0, close), frame).text; }
        catch (const std::exception& error) { result += "<" + std::string(error.what()) + ">"; }
        text.remove_prefix(close + 1);
    }
    return result;
}

bool Adapter::should_stop(const nvm::DebugPosition& position) {
    std::unique_lock lock(mutex_);
    if (terminate_.load()) return false;
    if (pause_.exchange(false)) { lock.unlock(); stopped("pause", position); return true; }
    const auto key = location_key(position);
    if (key.empty()) return false;
    if (stop_on_entry_) {
        stop_on_entry_ = false;
        lock.unlock(); stopped("entry", position); return true;
    }
    const auto* at = location(position.function, position.block);
    std::vector<int> hit_ids;
    std::string error;
    const auto found = sources_.find(at->path);
    if (found != sources_.end()) {
        const auto breakpoints = breakpoints_.find(found->second.path);
        if (breakpoints != breakpoints_.end()) {
            snapshot_ = execution_->debug_snapshot();
            FrameRef frame{};
            for (std::size_t i = 0; i < snapshot_.threads.size(); ++i)
                if (snapshot_.threads[i].id == position.task) { frame.thread = i; break; }
            for (auto& point : breakpoints->second) {
                if (!point.verified || point.line != static_cast<int>(at->line)) continue;
                try {
                    if (!hit(point, frame)) continue;
                    if (!point.log_message.empty()) {
                        event("output", Json{{"category", "console"}, {"output", interpolate(point.log_message, frame) + "\n"},
                                              {"source", source(*at)}, {"line", at->line}});
                    } else hit_ids.push_back(point.id);
                } catch (const std::exception& failure) {
                    hit_ids.push_back(point.id);
                    error = std::string("breakpoint evaluation failed: ") + failure.what();
                }
            }
        }
    }
    if (!hit_ids.empty()) { lock.unlock(); stopped("breakpoint", position, error, hit_ids); return true; }
    const bool changed = key != step_location_ || position.function != step_origin_.function || position.task != step_origin_.task;
    const bool step = step_ == Step::Into ? position.task == step_origin_.task && changed :
                      step_ == Step::Over ? position.task == step_origin_.task && position.depth <= step_origin_.depth && changed :
                      step_ == Step::Out ? position.task == step_origin_.task && position.depth < step_origin_.depth : false;
    if (step) { lock.unlock(); stopped("step", position); return true; }
    return false;
}

void Adapter::execute() {
    try {
        EventSink output(*this, "stdout"), error(*this, "stderr");
        nvm::DescriptorSource input(program_input_->descriptor());
        nvm::Capabilities capabilities;
        capabilities.fs = capabilities.net = capabilities.env = capabilities.clock = capabilities.entropy = true;
        // Native code can write the adapter's transport descriptors and is
        // not inspectable by this source debugger. Keep it explicitly denied.
        capabilities.native = false;
        const auto& cap = launch_arguments_["capabilities"];
        if (cap.is_object()) {
            capabilities.fs = cap["fs"].boolean(true); capabilities.net = cap["net"].boolean(true);
            capabilities.env = cap["env"].boolean(true); capabilities.clock = cap["clock"].boolean(true);
            capabilities.entropy = cap["entropy"].boolean(true);
        }
        nvm::ConsoleWriteLine console(output);
        nvm::StandardServices services(output, error, input, capabilities);
        nvm::HostRegistry registry;
        registry.add(console); services.register_all(registry);
        const auto bindings = nvm::bind(*image_, registry);
        auto modes = nvm::host_modes_for(*image_, bindings);
        nvm::RunLimits limits;
        limits.tier = 0;
        limits.jit = jit::Mode::Off;
        limits.host_modes = &modes;
        if (launch_arguments_["maxSteps"].is_number()) {
            const auto value = launch_arguments_["maxSteps"].integer();
            if (value <= 0) throw std::runtime_error("maxSteps must be positive");
            limits.max_steps = static_cast<std::uint64_t>(value);
        }
        auto execution = std::make_unique<nvm::Execution>(*image_, array_strings(launch_arguments_["args"]), limits);
        { const std::lock_guard lock(mutex_); execution_ = std::move(execution); }
        nvm::SteadyTaskClock clock;
        nvm::PendingRequests pending(clock);
        std::vector<std::int64_t> scratch, outputs;
        std::optional<nvm::Event> final;
        std::string previous_location;
        std::uint64_t previous_task = std::numeric_limits<std::uint64_t>::max();
        std::uint32_t previous_function = nvr::absent;
        std::size_t previous_depth = 0;
        std::set<std::uint32_t> line_blocks;
        const auto deliver = [&](std::vector<nvm::PendingRequests::Done> done) {
            for (const auto& answer : done) execution_->deliver(answer.sequence, answer.completion);
        };
        while (!terminate_.load()) {
            nvm::Execution::GraphStep graph_step;
            auto event_value = execution_->begin_step(graph_step);
            if (!event_value) {
                const auto position = execution_->debug_position();
                const auto key = location_key(position);
                // One breakpoint hit per dynamic source-line visit. Multiple
                // lowered blocks of the same statement are one source stop.
                const bool changed = key != previous_location || position.task != previous_task ||
                                     position.function != previous_function || position.depth != previous_depth;
                const bool repeated = !changed && line_blocks.contains(position.block);
                if (changed || repeated) line_blocks.clear();
                line_blocks.insert(position.block);
                if (changed || repeated || pause_.load()) should_stop(position);
                if (terminate_.load()) break;
                previous_location = key; previous_task = position.task;
                previous_function = position.function; previous_depth = position.depth;
                const auto& graph = image_->graphs[graph_step.graph];
                outputs.resize(graph.outputs.size());
                const auto overflow = nvm::evaluate_outputs(graph, graph_step.inputs, scratch, outputs);
                if (overflow) event_value = execution_->fail_step(*overflow);
                else event_value = execution_->finish_step(outputs);
            }
            if (!event_value) continue;
            const auto& at = *event_value;
            if (at.kind == nvm::Event::Kind::Completed || at.kind == nvm::Event::Kind::Trapped) { final = at; break; }
            if (at.kind == nvm::Event::Kind::HostRequest) {
                auto request = at.request;
                auto* service = bindings.services.at(request.import);
                if (at.overlapping) {
                    request.now = clock.now();
                    pending.add(request.sequence, service->begin(request), service->cancellable());
                } else if (service->overlapping()) {
                    // Serial images still issue exactly one request at a
                    // time. Use the provider's asynchronous form solely to
                    // keep adapter pause/disconnect responsive while waiting.
                    request.now = clock.now();
                    nvm::PendingRequests synchronous(clock);
                    synchronous.add(request.sequence, service->begin(request), service->cancellable());
                    while (!terminate_.load()) {
                        const auto answers = synchronous.poll();
                        if (!answers.empty()) { execution_->complete(answers.front().completion); break; }
                        if (pause_.exchange(false)) stopped("pause", execution_->debug_position(), "program is waiting for host input or a timer");
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                } else {
                    execution_->complete(service->invoke(request));
                }
            } else if (at.kind == nvm::Event::Kind::Cancel) pending.cancel(at.sequence);
            else {
                deliver(pending.poll());
                if (at.timer) execution_->advance_clock(clock.now());
                if (at.kind == nvm::Event::Kind::Waiting) {
                    // Poll in short intervals so disconnect and pause remain
                    // responsive while a socket, host request, or timer waits.
                    while (!terminate_.load()) {
                        const auto done = pending.poll();
                        if (!done.empty()) { deliver(done); break; }
                        if (at.timer && execution_->advance_clock(clock.now()) != 0) break;
                        if (pause_.exchange(false)) stopped("pause", execution_->debug_position(), "program is waiting for host I/O or a timer");
                        std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
            }
        }
        if (final && final->kind == nvm::Event::Kind::Trapped && !terminate_.load()) {
            { const std::lock_guard lock(mutex_); trap_ = final->trap; }
            const auto position = execution_->debug_position();
            if (trap_stops_) stopped("exception", position, nvm::describe(final->trap, *image_));
            event("output", Json{{"category", "stderr"}, {"output", nvm::describe(final->trap, *image_) + "\n"}});
        }
        services.end_run();
        const int status = final ? final->kind == nvm::Event::Kind::Completed ? final->status : nvm::exit_status(final->trap) : 0;
        { const std::lock_guard lock(mutex_); stopped_ = false; }
        event("exited", Json{{"exitCode", status}});
        event("terminated");
    } catch (const std::exception& error) {
        { const std::lock_guard lock(mutex_); stopped_ = false; }
        event("output", Json{{"category", "stderr"}, {"output", std::string(error.what()) + "\n"}});
        event("exited", Json{{"exitCode", 1}});
        event("terminated");
    }
}

Json Adapter::stack_trace(const Json& arguments) {
    const auto id = static_cast<std::uint64_t>(arguments["threadId"].integer(1) - 1);
    const auto thread = std::find_if(snapshot_.threads.begin(), snapshot_.threads.end(), [&](const auto& t) { return t.id == id; });
    if (thread == snapshot_.threads.end()) throw std::runtime_error("unknown task");
    const auto thread_index = static_cast<std::size_t>(thread - snapshot_.threads.begin());
    const auto start = static_cast<std::size_t>(std::max<std::int64_t>(0, arguments["startFrame"].integer()));
    const auto requested = arguments["levels"].integer(100);
    const auto count = requested <= 0 ? thread->frames.size() : static_cast<std::size_t>(requested);
    Json result = Json::Array{};
    for (auto i = start; i < thread->frames.size() && i - start < count; ++i) {
        const auto& frame = thread->frames[i];
        const int reference = reference_sequence_++;
        frames_.emplace(reference, FrameRef{thread_index, i});
        Json value{{"id", reference}, {"name", function_name(frame.function)}, {"line", 1}, {"column", 1}};
        if (const auto* at = location(frame.function, frame.block)) {
            value["source"] = source(*at); value["line"] = at->line;
            std::size_t column = at->column;
            if (const auto found = sources_.find(at->path); found != sources_.end() && found->second.snapshot)
                column = utf16_column(found->second.snapshot->line_text(at->line - 1), at->column);
            value["column"] = column;
        } else value["presentationHint"] = "subtle";
        result.push_back(std::move(value));
    }
    return Json{{"stackFrames", result}, {"totalFrames", thread->frames.size()}};
}

Adapter::FrameRef Adapter::find_frame(const Json& arguments) const {
    if (arguments["frameId"].is_number()) {
        const auto found = frames_.find(static_cast<int>(arguments["frameId"].integer()));
        if (found == frames_.end()) throw std::runtime_error("frame is no longer valid");
        return found->second;
    }
    for (std::size_t i = 0; i < snapshot_.threads.size(); ++i)
        if (snapshot_.threads[i].id == current_position_.task && !snapshot_.threads[i].frames.empty()) return {i, 0};
    throw std::runtime_error("no active frame");
}

Json Adapter::variables(const Json& arguments) {
    const auto found = variables_.find(static_cast<int>(arguments["variablesReference"].integer()));
    if (found == variables_.end()) throw std::runtime_error("variables are no longer valid");
    const auto reference = found->second;
    const auto start = static_cast<std::size_t>(std::max<std::int64_t>(0, arguments["start"].integer()));
    auto count = static_cast<std::size_t>(std::max<std::int64_t>(0, arguments["count"].integer(100)));
    if (count == 0) count = 1000;
    count = std::min<std::size_t>(count, 1000);
    Json result = Json::Array{};
    if (reference.kind == Variables::Kind::Locals) {
        const auto& frame = snapshot_.threads.at(reference.frame.thread).frames.at(reference.frame.frame);
        std::vector<std::pair<std::string, nvm::DebugValue>> values;
        for (std::size_t i = 0; i < frame.slots.size(); ++i) {
            if (!names_ || frame.function >= names_->slots.size() || i >= names_->slots[frame.function].size()) continue;
            const auto& name = names_->slots[frame.function][i];
            if (name.empty() || name.starts_with('$')) continue;
            values.emplace_back(name, frame.slots[i]);
        }
        for (auto i = start; i < values.size() && i - start < count; ++i)
            result.push_back(variable(values[i].first, values[i].second));
    } else if (reference.kind == Variables::Kind::Globals) {
        for (auto i = start; i < snapshot_.globals.size() && i - start < count; ++i) {
            const auto name = names_ && i < names_->globals.size() && !names_->globals[i].empty()
                ? names_->globals[i] : "global " + std::to_string(i);
            if (name.starts_with('$')) continue;
            result.push_back(variable(name, snapshot_.globals[i]));
        }
    } else {
        const auto values = execution_->debug_children(reference.value.type, reference.value.bits, start, count);
        for (std::size_t i = 0; i < values.size(); ++i) {
            std::string name = "[" + std::to_string(start + i) + "]";
            if (reference.value.layout != nvr::absent && names_ && reference.value.layout < names_->fields.size() &&
                start + i < names_->fields[reference.value.layout].size() && !names_->fields[reference.value.layout][start + i].empty())
                name = names_->fields[reference.value.layout][start + i];
            result.push_back(variable(std::move(name), values[i]));
        }
    }
    return Json{{"variables", result}};
}

nvm::DebugValue Adapter::expression(std::string_view text, const FrameRef& selected) {
    if (text.size() > 4096) throw std::runtime_error("debug expression is too long");
    // A deliberately read-only subset: names, direct object fields, indexed
    // values, Boolean logic and scalar comparisons. No calls, assignments,
    // casts or getter evaluation can cross into program execution.
    struct Parser {
        Adapter& adapter;
        std::string_view input;
        FrameRef frame;
        std::size_t position = 0;
        unsigned depth = 0;
        // Skipped logical operands are still parsed for valid syntax, but
        // never resolve names or inspect program storage.
        bool evaluating = true;
        void space() { while (position < input.size() && (input[position] == ' ' || input[position] == '\t')) ++position; }
        bool take(std::string_view token) {
            space();
            if (input.substr(position, token.size()) != token) return false;
            position += token.size(); return true;
        }
        std::string name() {
            space();
            const auto start = position;
            if (position == input.size() || !((input[position] >= 'a' && input[position] <= 'z') ||
                (input[position] >= 'A' && input[position] <= 'Z') || input[position] == '_'))
                throw std::runtime_error("expected a variable name");
            while (position < input.size() && ((input[position] >= 'a' && input[position] <= 'z') ||
                   (input[position] >= 'A' && input[position] <= 'Z') ||
                   (input[position] >= '0' && input[position] <= '9') || input[position] == '_')) ++position;
            return std::string(input.substr(start, position - start));
        }
        std::uint32_t type(nvr::TypeKind kind, unsigned width = 0) const {
            for (std::uint32_t i = 0; i < adapter.image_->types.size(); ++i)
                if (adapter.image_->types[i].kind == kind && (width == 0 || adapter.image_->types[i].width == width)) return i;
            return nvr::absent;
        }
        nvm::DebugValue boolean(bool value) const { return {type(nvr::TypeKind::Bool), value ? 1U : 0U, true, value ? "true" : "false", 0}; }
        bool truth(const nvm::DebugValue& value) const {
            if (value.text == "true") return true;
            if (value.text == "false") return false;
            throw std::runtime_error("Boolean operand required");
        }
        nvm::DebugValue lookup(const std::string& identifier) const {
            const auto& f = adapter.snapshot_.threads.at(frame.thread).frames.at(frame.frame);
            std::optional<nvm::DebugValue> found;
            if (adapter.names_ && f.function < adapter.names_->slots.size()) {
                const auto& names = adapter.names_->slots[f.function];
                for (std::size_t i = 0; i < names.size() && i < f.slots.size(); ++i) {
                    if (names[i] == identifier) {
                        // A later assigned shadow occupies a later slot.
                        if (!found || f.slots[i].initialized) found = f.slots[i];
                    }
                }
            }
            if (found) {
                if (!found->initialized) throw std::runtime_error(identifier + " is not initialized");
                return *found;
            }
            if (adapter.names_) {
                for (std::size_t i = 0; i < adapter.names_->globals.size() && i < adapter.snapshot_.globals.size(); ++i) {
                    const auto& global = adapter.names_->globals[i];
                    if (global == identifier || global.ends_with("." + identifier)) {
                        if (found) throw std::runtime_error("global name is ambiguous: " + identifier);
                        found = adapter.snapshot_.globals[i];
                    }
                }
            }
            if (!found) throw std::runtime_error("unknown variable: " + identifier);
            if (!found->initialized) throw std::runtime_error(identifier + " is not initialized");
            return *found;
        }
        nvm::DebugValue primary() {
            if (++depth > 32) throw std::runtime_error("debug expression nesting limit exceeded");
            nvm::DebugValue result;
            if (take("(")) {
                result = logical_or();
                if (!take(")")) throw std::runtime_error("expected ')'");
            } else {
                space();
                if (position < input.size() && ((input[position] >= '0' && input[position] <= '9') || input[position] == '-')) {
                    const auto start = position;
                    if (input[position] == '-') ++position;
                    while (position < input.size() && input[position] >= '0' && input[position] <= '9') ++position;
                    const auto digits = input.substr(start, position - start);
                    if (digits.starts_with('-')) {
                        std::int64_t value = 0;
                        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
                        if (error != std::errc{} || end != digits.data() + digits.size()) throw std::runtime_error("invalid signed integer literal");
                        result = {type(nvr::TypeKind::Signed, 64), static_cast<std::uint64_t>(value), true, std::string(digits), 0};
                    } else {
                        std::uint64_t value = 0;
                        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
                        if (error != std::errc{} || end != digits.data() + digits.size()) throw std::runtime_error("invalid unsigned integer literal");
                        result = {type(nvr::TypeKind::Unsigned, 64), value, true, std::string(digits), 0};
                    }
                } else {
                    const auto identifier = name();
                    if (identifier == "true" || identifier == "false") result = boolean(identifier == "true");
                    else result = evaluating ? lookup(identifier) : boolean(false);
                }
            }
            for (;;) {
                if (take(".")) {
                    const auto field = name();
                    if (!evaluating) continue;
                    const auto kind = result.type < adapter.image_->types.size()
                        ? adapter.image_->types[result.type].kind : nvr::TypeKind::Void;
                    if (field == "length" && (kind == nvr::TypeKind::Array || kind == nvr::TypeKind::List || kind == nvr::TypeKind::Bytes)) {
                        result = {type(nvr::TypeKind::Unsigned, 64), result.children, true, std::to_string(result.children), 0};
                        continue;
                    }
                    if (result.layout == nvr::absent || !adapter.names_ || result.layout >= adapter.names_->fields.size())
                        throw std::runtime_error("only direct object storage can be inspected; getters are not evaluated");
                    const auto& names = adapter.names_->fields[result.layout];
                    const auto found = std::find(names.begin(), names.end(), field);
                    if (found == names.end()) throw std::runtime_error("unknown stored field: " + field);
                    const auto index = static_cast<std::size_t>(found - names.begin());
                    const auto values = adapter.execution_->debug_children(result.type, result.bits, index, 1);
                    if (values.empty() || !values.front().initialized) throw std::runtime_error("field is not initialized");
                    result = values.front();
                } else if (take("[")) {
                    const auto index_value = logical_or();
                    if (!take("]")) throw std::runtime_error("expected ']'");
                    if (!evaluating) continue;
                    std::uint64_t index = 0;
                    const auto [end, error] = std::from_chars(index_value.text.data(), index_value.text.data() + index_value.text.size(), index);
                    if (error != std::errc{} || end != index_value.text.data() + index_value.text.size() || index >= result.children)
                        throw std::runtime_error("debug index is out of bounds");
                    const auto values = adapter.execution_->debug_children(result.type, result.bits, static_cast<std::size_t>(index), 1);
                    if (values.empty() || !values.front().initialized) throw std::runtime_error("element is not initialized");
                    result = values.front();
                } else break;
            }
            --depth;
            return result;
        }
        nvm::DebugValue unary() {
            if (take("!")) {
                const auto operand = unary();
                return boolean(evaluating && !truth(operand));
            }
            return primary();
        }
        nvm::DebugValue comparison() {
            auto left = unary();
            std::string_view op;
            for (auto candidate : {std::string_view("=="), std::string_view("!="), std::string_view("<="), std::string_view(">="), std::string_view("<"), std::string_view(">")})
                if (take(candidate)) { op = candidate; break; }
            if (op.empty()) return left;
            const auto right = unary();
            if (!evaluating) return boolean(false);
            // Exact decimal integer comparison without floating conversion:
            // this remains precise for uint64 even on hosts with 64-bit
            // long double. Boolean equality is handled separately.
            bool equal = false, less = false;
            if ((left.text == "true" || left.text == "false") && (right.text == "true" || right.text == "false")) {
                equal = truth(left) == truth(right); less = !truth(left) && truth(right);
            } else {
                const auto parse_integer = [](std::string_view text) {
                    bool negative = text.starts_with('-'); if (negative) text.remove_prefix(1);
                    std::uint64_t magnitude = 0;
                    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), magnitude);
                    if (error != std::errc{} || end != text.data() + text.size())
                        throw std::runtime_error("debug comparisons currently require integer or Boolean operands");
                    return std::pair{negative && magnitude != 0, magnitude};
                };
                const auto a = parse_integer(left.text), b = parse_integer(right.text);
                equal = a == b;
                less = a.first != b.first ? a.first : a.first ? a.second > b.second : a.second < b.second;
            }
            return boolean(op == "==" ? equal : op == "!=" ? !equal : op == "<" ? less :
                           op == "<=" ? less || equal : op == ">" ? !less && !equal : !less);
        }
        nvm::DebugValue logical_and() {
            auto result = comparison();
            while (take("&&")) {
                const bool parent = evaluating;
                const bool left = parent && truth(result);
                evaluating = parent && left;
                const auto right = comparison();
                evaluating = parent;
                result = boolean(parent && left && truth(right));
            }
            return result;
        }
        nvm::DebugValue logical_or() {
            auto result = logical_and();
            while (take("||")) {
                const bool parent = evaluating;
                const bool left = parent && truth(result);
                evaluating = parent && !left;
                const auto right = logical_and();
                evaluating = parent;
                result = boolean(parent && (left || truth(right)));
            }
            return result;
        }
    };
    Parser parser{*this, text, selected};
    auto result = parser.logical_or();
    parser.space();
    if (parser.position != text.size()) throw std::runtime_error("unsupported debug expression; calls and assignments are disabled");
    return result;
}

Json Adapter::evaluate(const Json& arguments) {
    const auto value = expression(arguments["expression"].string(), find_frame(arguments));
    auto result = variable({}, value);
    result["result"] = result["value"];
    return result;
}

void Adapter::handle(const Json& request) {
    const auto command = request["command"].string();
    const auto& args = request["arguments"];
    if (command == "initialize") {
        response(request, Json{{"supportsConfigurationDoneRequest", true}, {"supportsConditionalBreakpoints", true},
            {"supportsHitConditionalBreakpoints", true}, {"supportsLogPoints", true},
            {"supportsEvaluateForHovers", true}, {"supportsTerminateRequest", true},
            {"supportsExceptionInfoRequest", true}, {"supportsLoadedSourcesRequest", true},
            {"supportsBreakpointLocationsRequest", true}, {"supportsValueFormattingOptions", false},
            {"supportsSetVariable", false}, {"supportsSetExpression", false},
            {"supportsStepBack", false}, {"supportsRestartRequest", false},
            {"exceptionBreakpointFilters", Json::Array{Json{{"filter", "traps"}, {"label", "Runtime traps"}, {"default", true}}}}});
    } else if (command == "launch") {
        launch(args);
        response(request);
        event("initialized");
    } else if (command == "attach") {
        throw std::runtime_error("attach is unavailable; launch a Neura source or .nvr image");
    } else if (command == "configurationDone") {
        if (!launched_) throw std::runtime_error("launch a program before configurationDone");
        if (configured_) throw std::runtime_error("configurationDone was already received");
        configured_ = true;
        response(request);
        worker_ = std::thread([this] { execute(); });
    } else if (command == "disconnect" || command == "terminate") {
        response(request);
        terminate();
        if (!worker_.joinable() && !configured_) event("terminated");
    } else if (command == "pause") {
        pause_.store(true);
        response(request);
    } else if (command == "neura/input") {
        if (!program_input_) throw std::runtime_error("no debuggee input is available");
        if (args["eof"].boolean()) program_input_->close_input();
        else program_input_->write(args["text"].string());
        response(request);
    } else if (command == "evaluate" && args["context"].string() == "repl" && args["expression"].string().starts_with(":input ")) {
        if (!program_input_) throw std::runtime_error("no debuggee input is available");
        program_input_->write(args["expression"].string().substr(7) + "\n");
        response(request, Json{{"result", "input sent"}, {"variablesReference", 0}});
    } else if (command == "source") {
        const auto reference = args["sourceReference"].integer(args["source"]["sourceReference"].integer());
        for (const auto& [label, item] : sources_) {
            (void)label;
            if (item.reference == reference && item.snapshot) {
                response(request, Json{{"content", item.snapshot->text()}, {"mimeType", "text/x-neura"}});
                return;
            }
        }
        throw std::runtime_error("compiled source snapshot is unavailable");
    } else if (command == "loadedSources") {
        Json sources = Json::Array{};
        std::set<std::string> seen;
        if (image_ && image_->source_map) for (const auto& at : *image_->source_map)
            if (seen.insert(at.path).second) sources.push_back(source(at));
        response(request, Json{{"sources", sources}});
    } else if (command == "setExceptionBreakpoints") {
        const std::lock_guard lock(mutex_);
        trap_stops_ = std::any_of(args["filters"].items().begin(), args["filters"].items().end(),
                                  [](const auto& filter) { return filter.string() == "traps"; });
        response(request, Json{{"breakpoints", Json::Array{}}});
    } else if (command == "setBreakpoints") {
        const std::lock_guard lock(mutex_);
        response(request, set_breakpoints(args));
    } else if (command == "breakpointLocations") {
        const std::lock_guard lock(mutex_);
        Json result = Json::Array{};
        const auto path = path_key(args["source"]["path"].string(), cwd_.empty() ? fs::current_path() : cwd_);
        const auto first = args["line"].integer(1), last = args["endLine"].integer(first);
        std::set<std::pair<unsigned, std::size_t>> seen;
        if (image_ && image_->source_map) for (const auto& at : *image_->source_map) {
            const auto found = sources_.find(at.path);
            if (found == sources_.end() || found->second.path != path || at.line < first || at.line > last) continue;
            std::size_t column = at.column;
            if (found->second.snapshot)
                column = utf16_column(found->second.snapshot->line_text(at.line - 1), at.column);
            if (seen.emplace(at.line, column).second) result.push_back(Json{{"line", at.line}, {"column", column}});
        }
        response(request, Json{{"breakpoints", result}});
    } else {
        const std::lock_guard lock(mutex_);
        if (command == "threads") {
            Json threads = Json::Array{};
            for (const auto& thread : snapshot_.threads)
                threads.push_back(Json{{"id", thread.id + 1}, {"name", "task " + std::to_string(thread.id) + " · " + thread.state}});
            if (threads.size() == 0 && launched_) threads.push_back(Json{{"id", 1}, {"name", "main task"}});
            response(request, Json{{"threads", threads}});
            return;
        }
        if (!stopped_) throw std::runtime_error("this request requires a stopped program");
        if (command == "continue" || command == "next" || command == "stepIn" || command == "stepOut") {
            const auto task = static_cast<std::uint64_t>(args["threadId"].integer(static_cast<std::int64_t>(current_position_.task + 1)) - 1);
            resume(command == "next" ? Step::Over : command == "stepIn" ? Step::Into : command == "stepOut" ? Step::Out : Step::Continue, task);
            response(request, command == "continue" ? Json{{"allThreadsContinued", true}} : Json::Object{});
            event("continued", Json{{"threadId", current_position_.task + 1}, {"allThreadsContinued", true}});
        } else if (command == "stackTrace") response(request, stack_trace(args));
        else if (command == "scopes") {
            const auto frame = find_frame(args);
            const auto locals = reference_sequence_++, globals = reference_sequence_++;
            variables_.emplace(locals, Variables{Variables::Kind::Locals, frame, {}});
            variables_.emplace(globals, Variables{Variables::Kind::Globals, frame, {}});
            response(request, Json{{"scopes", Json::Array{
                Json{{"name", "Locals"}, {"presentationHint", "locals"}, {"variablesReference", locals}, {"expensive", false}},
                Json{{"name", "Globals"}, {"presentationHint", "globals"}, {"variablesReference", globals}, {"expensive", false}}}}});
        } else if (command == "variables") response(request, variables(args));
        else if (command == "evaluate") response(request, evaluate(args));
        else if (command == "exceptionInfo") {
            if (!trap_) throw std::runtime_error("the program is not stopped on a trap");
            response(request, Json{{"exceptionId", nvm::describe(trap_->kind)},
                {"description", nvm::describe(*trap_, *image_)}, {"breakMode", "unhandled"},
                {"details", Json{{"message", trap_->detail}, {"typeName", nvm::describe(trap_->kind)}}}});
        } else throw std::runtime_error("unsupported debug request: " + command);
    }
}

} // namespace

int run_debug(std::istream& input, std::ostream& output, const std::filesystem::path& executable) {
    return Adapter(input, output, executable).run();
}

} // namespace neura::tooling
