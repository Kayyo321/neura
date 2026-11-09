#include "host.hpp"

#include <algorithm>

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

// Host signatures name types structurally; image types also refer to other
// table entries, which host signatures do not support.
bool same_host_type(const nvr::Image& image, std::uint32_t index, const nvr::Type& expected) {
    const auto& actual = image.types[index];
    if (actual.kind != expected.kind || actual.width != expected.width) {
        return false;
    }
    switch (actual.kind) {
    case nvr::TypeKind::Object:
    case nvr::TypeKind::Array:
    case nvr::TypeKind::List:
        return false;
    default:
        return true;
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

bool StreamSink::write(std::string_view bytes) {
    const auto written = std::fwrite(bytes.data(), 1, bytes.size(), stream_);
    return written == bytes.size() && std::fflush(stream_) == 0;
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

TestAssert::TestAssert(TestLog& log) : log_(log) {
    signature_.name = "neura.test.assert";
    signature_.version = 1;
    signature_.parameters = {nvr::Type{nvr::TypeKind::Bool, 0, 0}};
    signature_.success = nvr::Type{nvr::TypeKind::Void, 0, 0};
    signature_.error = nvr::Type{nvr::TypeKind::Unsigned, 32, 0};
}

HostCompletion TestAssert::invoke(const HostRequest& request) {
    if (request.arguments.at(0).bits != 0) {
        return HostCompletion::ok();
    }
    if (!log_.failure) {
        log_.failure = "assertion failed";
    }
    return HostCompletion::failed(HostValue{assertion_failed, {}});
}

TestAssertEqualInt32::TestAssertEqualInt32(TestLog& log) : log_(log) {
    signature_.name = "neura.test.assert_equal_int32";
    signature_.version = 1;
    signature_.parameters = {nvr::Type{nvr::TypeKind::Signed, 32, 0}, nvr::Type{nvr::TypeKind::Signed, 32, 0}};
    signature_.success = nvr::Type{nvr::TypeKind::Void, 0, 0};
    signature_.error = nvr::Type{nvr::TypeKind::Unsigned, 32, 0};
}

HostCompletion TestAssertEqualInt32::invoke(const HostRequest& request) {
    const auto value = [&](std::size_t i) {
        return static_cast<std::int32_t>(static_cast<std::uint32_t>(request.arguments.at(i).bits));
    };
    const auto expected = value(0);
    const auto actual = value(1);
    if (expected == actual) {
        return HostCompletion::ok();
    }
    if (!log_.failure) {
        log_.failure = "expected " + std::to_string(expected) + ", got " + std::to_string(actual);
    }
    return HostCompletion::failed(HostValue{assertion_failed, {}});
}

} // namespace neura::nvm
