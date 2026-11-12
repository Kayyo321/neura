#include "test_protocol.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <exception>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace neura::tooling {
namespace {

int duplicate(int descriptor) {
#ifdef _WIN32
    return _dup(descriptor);
#else
    return dup(descriptor);
#endif
}
int descriptor(std::FILE* stream) {
#ifdef _WIN32
    return _fileno(stream);
#else
    return fileno(stream);
#endif
}
int replace(int from, int to) {
#ifdef _WIN32
    return _dup2(from, to);
#else
    return dup2(from, to) < 0 ? -1 : 0;
#endif
}
void close_descriptor(int value) {
#ifdef _WIN32
    _close(value);
#else
    close(value);
#endif
}
std::FILE* open_descriptor(int value) {
#ifdef _WIN32
    return _fdopen(value, "wb");
#else
    return fdopen(value, "wb");
#endif
}

std::string qualified_name(const IrProgram& program, SymbolId id) {
    auto name = program.symbols[id].name;
    for (auto owner = program.symbols[id].owner; owner < program.symbols.size();
         owner = program.symbols[owner].owner) {
        if (program.symbols[owner].kind == SymbolKind::File) break;
        name = program.symbols[owner].name + "." + name;
    }
    return name;
}

} // namespace

std::vector<TestInfo> discover_tests(const IrProgram& program,
                                     const std::vector<std::filesystem::path>& inputs,
                                     const std::filesystem::path& base) {
    namespace fs = std::filesystem;
    std::vector<TestInfo> tests;
    for (SymbolId id = 0; id < program.symbols.size(); ++id) {
        const auto& item = program.symbols[id];
        if (item.kind != SymbolKind::Function || !item.exported || !item.has_body ||
            !item.name.starts_with("test_") || !item.parameters.empty() || !item.generics.empty() ||
            item.type.kind != TypeKind::Void || item.owner >= program.symbols.size() ||
            program.symbols[item.owner].kind != SymbolKind::Module ||
            is_static_scope(program.symbols[item.owner]) || !program.symbols[item.owner].generics.empty()) continue;
        const auto file = fs::path(item.span.file);
        bool listed = false;
        for (const auto& input : inputs) {
            std::error_code error;
            if (fs::equivalent(input, file, error) && !error) { listed = true; break; }
        }
        if (!listed) continue;
        std::error_code error;
        auto absolute = fs::weakly_canonical(file, error);
        if (error) absolute = fs::absolute(file).lexically_normal();
        auto relative = fs::relative(absolute, base, error);
        if (error) relative = absolute;
        const auto path = relative.generic_string();
        const auto qualified = qualified_name(program, id);
        tests.push_back({id, path + "::" + qualified, qualified,
                         program.symbols[item.owner].name + "." + item.name,
                         absolute.generic_string(), path, item.span});
    }
    return tests;
}

Json test_info_json(const TestInfo& test) {
    return Json{{"id", test.id}, {"name", test.name}, {"label", test.label},
                {"file", test.file}, {"path", test.path}, {"line", test.span.line},
                {"column", test.span.column}, {"offset", test.span.offset}, {"length", test.span.length}};
}

// Text output remains valid UTF-8 even when native code writes arbitrary
// bytes. Preserve invalid/binary chunks as base64 alongside the display text.
Json test_output_json(std::string_view id, std::string_view stream, std::string_view bytes) {
    std::string text;
    bool repaired = false;
    for (std::size_t i = 0; i < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[i]);
        std::size_t length = lead < 0x80 ? 1 : lead >= 0xc2 && lead <= 0xdf ? 2 :
                             lead >= 0xe0 && lead <= 0xef ? 3 : lead >= 0xf0 && lead <= 0xf4 ? 4 : 0;
        bool valid = length != 0 && i + length <= bytes.size();
        for (std::size_t n = 1; valid && n < length; ++n)
            valid = (static_cast<unsigned char>(bytes[i + n]) & 0xc0) == 0x80;
        if (valid && length >= 3) {
            const auto next = static_cast<unsigned char>(bytes[i + 1]);
            valid = !(lead == 0xe0 && next < 0xa0) && !(lead == 0xed && next >= 0xa0) &&
                    !(lead == 0xf0 && next < 0x90) && !(lead == 0xf4 && next >= 0x90);
        }
        if (valid) { text.append(bytes.substr(i, length)); i += length; }
        else { text += "\xef\xbf\xbd"; ++i; repaired = true; }
    }
    Json event{{"id", id}, {"stream", stream}, {"output", text}};
    if (repaired) {
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string data;
        for (std::size_t i = 0; i < bytes.size(); i += 3) {
            const auto a = static_cast<unsigned char>(bytes[i]);
            const auto b = i + 1 < bytes.size() ? static_cast<unsigned char>(bytes[i + 1]) : 0;
            const auto c = i + 2 < bytes.size() ? static_cast<unsigned char>(bytes[i + 2]) : 0;
            data += alphabet[a >> 2];
            data += alphabet[((a & 3) << 4) | (b >> 4)];
            data += i + 1 < bytes.size() ? alphabet[((b & 15) << 2) | (c >> 6)] : '=';
            data += i + 2 < bytes.size() ? alphabet[c & 63] : '=';
        }
        event["encoding"] = "base64";
        event["data"] = std::move(data);
    }
    return event;
}

TestReporter::TestReporter() {
    std::cout.flush();
    std::fflush(stdout);
    const int copy = duplicate(descriptor(stdout));
    if (copy < 0) throw std::runtime_error("cannot duplicate test reporter output");
    stream_ = open_descriptor(copy);
    if (!stream_) {
        close_descriptor(copy);
        throw std::runtime_error("cannot open test reporter output");
    }
}
TestReporter::~TestReporter() { if (stream_) std::fclose(stream_); }
void TestReporter::event(std::string_view kind, Json data) {
    data["version"] = 1;
    data["event"] = kind;
    const auto bytes = data.dump() + "\n";
    if (std::fwrite(bytes.data(), 1, bytes.size(), stream_) != bytes.size() || std::fflush(stream_) != 0)
        throw std::runtime_error("cannot write test reporter output");
}

struct TestOutputCapture::Live {
    Emitter emit;
    std::atomic<bool> stopped{false};
    std::thread worker;
    std::exception_ptr failure;
};

namespace {
void emit_complete_utf8(std::string_view name, std::string& pending, bool final,
                        const TestOutputCapture::Emitter& emit) {
    auto end = pending.size();
    if (!final && end != 0) {
        auto lead = end - 1;
        while (lead > 0 && (static_cast<unsigned char>(pending[lead]) & 0xc0) == 0x80) --lead;
        const auto first = static_cast<unsigned char>(pending[lead]);
        const std::size_t expected = first >= 0xc2 && first <= 0xdf ? 2 :
                                     first >= 0xe0 && first <= 0xef ? 3 :
                                     first >= 0xf0 && first <= 0xf4 ? 4 : 1;
        if (end - lead < expected) end = lead;
    }
    if (end) { emit(name, std::string_view(pending).substr(0, end)); pending.erase(0, end); }
}
} // namespace

TestOutputCapture::TestOutputCapture(Emitter live) {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(stdout);
    std::fflush(stderr);
    output_ = std::tmpfile();
    error_ = std::tmpfile();
    saved_output_ = duplicate(descriptor(stdout));
    saved_error_ = duplicate(descriptor(stderr));
    if (!output_ || !error_ || saved_output_ < 0 || saved_error_ < 0 ||
        replace(descriptor(output_), descriptor(stdout)) < 0 ||
        replace(descriptor(error_), descriptor(stderr)) < 0) {
        restore();
        if (output_) std::fclose(output_);
        if (error_) std::fclose(error_);
        throw std::runtime_error("cannot capture test output");
    }
    if (live) {
        live_ = std::make_unique<Live>();
        live_->emit = std::move(live);
#ifndef _WIN32
        try {
            live_->worker = std::thread([this] {
                struct Stream {
                    int file;
                    const char* name;
                    off_t offset = 0;
                    std::string pending;
                };
                std::array<Stream, 2> streams{{{descriptor(output_), "stdout", 0, {}},
                                              {descriptor(error_), "stderr", 0, {}}}};
                std::array<char, 16384> chunk{};
                try {
                    for (;;) {
                        const bool stopping = live_->stopped.load(std::memory_order_acquire);
                        for (auto& stream : streams) {
                            // pread never changes the offset used by the
                            // program's stdout/stderr writes.
                            for (unsigned round = 0; stopping || round < 16; ++round) {
                                const auto size = ::pread(stream.file, chunk.data(), chunk.size(), stream.offset);
                                if (size < 0) throw std::runtime_error("cannot read captured test output");
                                if (size == 0) break;
                                stream.offset += size;
                                stream.pending.append(chunk.data(), static_cast<std::size_t>(size));
                                emit_complete_utf8(stream.name, stream.pending, false, live_->emit);
                            }
                            if (stopping) emit_complete_utf8(stream.name, stream.pending, true, live_->emit);
                        }
                        if (stopping) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                } catch (...) { live_->failure = std::current_exception(); }
            });
        } catch (...) {
            restore();
            std::fclose(output_);
            std::fclose(error_);
            throw;
        }
#endif
    }
}
void TestOutputCapture::restore() noexcept {
    std::fflush(stdout);
    std::fflush(stderr);
    if (saved_output_ >= 0) {
        replace(saved_output_, descriptor(stdout));
        close_descriptor(saved_output_);
        saved_output_ = -1;
    }
    if (saved_error_ >= 0) {
        replace(saved_error_, descriptor(stderr));
        close_descriptor(saved_error_);
        saved_error_ = -1;
    }
}
void TestOutputCapture::stop_live() noexcept {
    if (live_) {
        live_->stopped.store(true, std::memory_order_release);
        if (live_->worker.joinable()) live_->worker.join();
    }
}
TestOutputCapture::~TestOutputCapture() {
    restore();
    stop_live();
    if (output_) std::fclose(output_);
    if (error_) std::fclose(error_);
}
void TestOutputCapture::finish(const Emitter& emit) {
    std::cout.flush();
    std::cerr.flush();
    restore();
#ifndef _WIN32
    if (live_) {
        stop_live();
        if (live_->failure) std::rethrow_exception(live_->failure);
        return;
    }
#endif
    const auto& output = live_ ? live_->emit : emit;
    std::array<char, 16384> chunk{};
    for (const auto& [stream, name] : {std::pair{output_, "stdout"}, std::pair{error_, "stderr"}}) {
        if (std::fflush(stream) != 0 || std::fseek(stream, 0, SEEK_SET) != 0)
            throw std::runtime_error("cannot read captured test output");
        std::string pending;
        for (;;) {
            const auto size = std::fread(chunk.data(), 1, chunk.size(), stream);
            pending.append(chunk.data(), size);
            emit_complete_utf8(name, pending, size != chunk.size(), output);
            if (size != chunk.size()) {
                if (std::ferror(stream)) throw std::runtime_error("cannot read captured test output");
                break;
            }
        }
    }
}

} // namespace neura::tooling
