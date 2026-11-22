#include "host.hpp"

#include <algorithm>

#ifdef _WIN32
#include "platform/windows.hpp"

#include <cstdlib>
#include <io.h>
#endif

namespace neura::nvm {

void HostRegistry::add(HostService& service) {
    const auto& signature = service.signature();
    services_[{signature.name, signature.version}] = &service;
}

HostService* HostRegistry::find(std::string_view name, std::uint32_t version) const {
    const auto it = services_.find(std::make_pair(std::string(name), version));
    return it == services_.end() ? nullptr : it->second;
}

namespace {

// Image-local layout IDs and type indices do not cross the host boundary.
bool same_host_type(const nvr::Image& image, std::uint32_t index, const HostType& expected) {
    const auto& actual = image.types[index];
    if (actual.kind != expected.kind || actual.width != expected.width) {
        return false;
    }
    switch (actual.kind) {
    case nvr::TypeKind::Object: {
        const auto& layout = image.layouts[actual.reference];
        if (layout.parent != nvr::absent || layout.fields.size() != expected.components.size()) return false;
        for (std::size_t i = 0; i < layout.fields.size(); ++i) {
            if (!same_host_type(image, layout.fields[i], expected.components[i])) return false;
        }
        return true;
    }
    case nvr::TypeKind::Array:
    case nvr::TypeKind::List:
        return expected.components.size() == 1 &&
               same_host_type(image, actual.reference, expected.components.front());
    default:
        return expected.components.empty();
    }
}

} // namespace

Bindings bind(const nvr::Image& image, const HostRegistry& registry) {
    Bindings result;
    for (std::size_t i = 0; i < image.imports.size(); ++i) {
        const auto& import = image.imports[i];
        const auto label = import.name + " version " + std::to_string(import.interface_version);
        auto* service = registry.find(import.name, import.interface_version);
        if (service == nullptr) {
            throw BindError("import " + std::to_string(i) + " (" + label + ") is not available");
        }
        const auto& signature = service->signature();
        bool matches = import.parameter_types.size() == signature.parameters.size() &&
                       same_host_type(image, import.success_type, signature.success) &&
                       same_host_type(image, import.error_type, signature.error);
        for (std::size_t p = 0; matches && p < signature.parameters.size(); ++p) {
            matches = same_host_type(image, import.parameter_types[p], signature.parameters[p]);
        }
        if (!matches) {
            throw BindError("import " + std::to_string(i) + " (" + label +
                            ") does not match the signature of the registered service");
        }
        result.services.push_back(service);
    }
    return result;
}

#ifdef _WIN32

namespace {

void ignore_invalid_parameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned, std::uintptr_t) {}

// The console a C stream writes to, or nullptr for a pipe, a file or NUL.
HANDLE console_of(std::FILE* stream) {
    if (stream == nullptr) return nullptr;
    const int descriptor = ::_fileno(stream);
    if (descriptor < 0) return nullptr;
    // A descriptor that is not open answers -1 instead of ending the process
    // through the C runtime's invalid-parameter handler.
    const auto previous = ::_set_thread_local_invalid_parameter_handler(ignore_invalid_parameter);
    const auto handle = ::_get_osfhandle(descriptor);
    ::_set_thread_local_invalid_parameter_handler(previous);
    if (handle == -1 || handle == -2 || handle == 0) return nullptr;
    DWORD mode = 0;
    if (!::GetConsoleMode(reinterpret_cast<HANDLE>(handle), &mode)) return nullptr;
    return reinterpret_cast<HANDLE>(handle);
}

// The length of the longest prefix of `bytes` that does not end inside a
// UTF-8 sequence.
std::size_t complete_length(std::string_view bytes) {
    const auto size = bytes.size();
    for (std::size_t back = 1; back <= 4 && back <= size; ++back) {
        const auto byte = static_cast<unsigned char>(bytes[size - back]);
        if ((byte & 0xC0u) == 0x80u) continue; // A continuation byte.
        std::size_t length = 1;
        if (byte >= 0xF0 && byte <= 0xF4) {
            length = 4;
        } else if (byte >= 0xE0 && byte < 0xF0) {
            length = 3;
        } else if (byte >= 0xC2 && byte < 0xE0) {
            length = 2;
        }
        return length > back ? size - back : size;
    }
    return size;
}

// Writes UTF-8 to a console as UTF-16 (not strictly: an invalid byte shows
// as U+FFFD). Returns how many of the bytes reached the console.
std::size_t write_utf8(HANDLE console, std::string_view bytes) {
    if (bytes.empty()) return 0;
    const int size = ::MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (size <= 0) return 0;
    std::wstring text(static_cast<std::size_t>(size), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), size);
    std::size_t done = 0;
    while (done < text.size()) {
        DWORD wrote = 0;
        const auto want = static_cast<DWORD>(std::min<std::size_t>(text.size() - done, 8192));
        if (!::WriteConsoleW(console, text.data() + done, want, &wrote, nullptr) || wrote == 0) break;
        done += wrote;
    }
    if (done == text.size()) return bytes.size();
    // The UTF-8 length of what was written; exact for valid UTF-8.
    const int written = ::WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(done), nullptr, 0, nullptr,
                                              nullptr);
    return std::min(bytes.size(), static_cast<std::size_t>(std::max(written, 0)));
}

} // namespace

StreamSink::StreamSink(std::FILE* stream) : stream_(stream), console_(console_of(stream)) {}

StreamSink::~StreamSink() {
    if (console_ != nullptr && !partial_.empty()) write_utf8(console_, partial_);
}

SinkWrite StreamSink::write_console(std::string_view bytes) {
    // Bytes that the C stream still buffers come first.
    std::fflush(stream_);
    std::string text = std::move(partial_);
    partial_.clear();
    const auto held = text.size();
    text.append(bytes);
    const auto complete = complete_length(text);
    const auto written = write_utf8(console_, std::string_view(text).substr(0, complete));
    if (written < complete) {
        return {written > held ? written - held : 0, true};
    }
    partial_ = text.substr(complete);
    return {bytes.size(), false};
}

#endif

bool StreamSink::write(std::string_view bytes) {
#ifdef _WIN32
    if (console_ != nullptr) return !write_console(bytes).failed;
#endif
    const auto written = std::fwrite(bytes.data(), 1, bytes.size(), stream_);
    return written == bytes.size() && std::fflush(stream_) == 0;
}

SinkWrite ConsoleSink::write_some(std::string_view bytes) {
    if (write(bytes)) return {bytes.size(), false};
    return {0, true};
}

SinkWrite StreamSink::write_some(std::string_view bytes) {
#ifdef _WIN32
    if (console_ != nullptr) return write_console(bytes);
#endif
    const auto written = std::fwrite(bytes.data(), 1, bytes.size(), stream_);
    const bool flushed = std::fflush(stream_) == 0;
    return {written, written != bytes.size() || !flushed};
}

SinkWrite StringSink::write_some(std::string_view bytes) {
    const auto before = contents_.size();
    const bool complete = write(bytes);
    return {contents_.size() - before, !complete};
}

bool StringSink::write(std::string_view bytes) {
    if (capacity_) {
        const auto room = *capacity_ - std::min(*capacity_, contents_.size());
        contents_.append(bytes.substr(0, room));
        return bytes.size() <= room;
    }
    contents_.append(bytes);
    return true;
}

ConsoleWriteLine::ConsoleWriteLine(ConsoleSink& sink) : sink_(sink) {
    signature_.name = "neura.console.write_line";
    signature_.version = 1;
    signature_.parameters = {nvr::Type{nvr::TypeKind::Text, 0, 0}};
    signature_.success = nvr::Type{nvr::TypeKind::Void, 0, 0};
    signature_.error = nvr::Type{nvr::TypeKind::Unsigned, 32, 0};
}

HostCompletion ConsoleWriteLine::invoke(const HostRequest& request) {
    std::string line = request.arguments.at(0).bytes;
    line.push_back('\n');
    if (sink_.write(line)) {
        return HostCompletion::ok();
    }
    return HostCompletion::failed(HostValue{console_write_failed, {}});
}

} // namespace neura::nvm
