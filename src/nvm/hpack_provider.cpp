#include "hpack_provider.hpp"

#include "nvr/format.hpp"

#include <memory>
#include <string_view>

#ifdef NEURA_NVM_HTTP2
#include <nghttp2/nghttp2.h>
#endif

namespace neura::nvm::provider {
namespace {

// Alternating name/value text keeps tuples out of the host-call collection boundary.
HostType fields_type() { return text_list(); }

#ifdef NEURA_NVM_HTTP2
constexpr std::size_t maximum_fields = 256;
constexpr std::size_t maximum_bytes = 65536;

bool field_name(std::string_view name) {
    if (name.empty()) return false;
    std::size_t start = name.front() == ':' ? 1 : 0;
    if (start == name.size()) return false;
    for (std::size_t i = start; i < name.size(); ++i) {
        const auto c = static_cast<unsigned char>(name[i]);
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string_view::npos) continue;
        return false;
    }
    return true;
}

bool field_value(std::string_view value) {
    if (!nvr::valid_utf8(value)) return false;
    for (const unsigned char c : value) if ((c < 32 && c != '\t') || c == 127) return false;
    return true;
}

HostCompletion encode(const HostValue& fields) {
    if (fields.elements.size() % 2 != 0) return fail(host_error::invalid_input);
    if (fields.elements.size() / 2 > maximum_fields) return fail(host_error::too_large);
    std::vector<nghttp2_nv> names;
    names.reserve(fields.elements.size() / 2);
    std::size_t size = 0;
    for (std::size_t i = 0; i < fields.elements.size(); i += 2) {
        const auto& name = fields.elements[i].bytes;
        const auto& value = fields.elements[i + 1].bytes;
        if (!field_name(name) || !field_value(value)) return fail(host_error::invalid_input);
        if (name.size() > maximum_bytes - size) return fail(host_error::too_large);
        size += name.size();
        if (value.size() > maximum_bytes - size) return fail(host_error::too_large);
        size += value.size();
        names.push_back({reinterpret_cast<std::uint8_t*>(const_cast<char*>(name.data())),
                         reinterpret_cast<std::uint8_t*>(const_cast<char*>(value.data())),
                         name.size(), value.size(), NGHTTP2_NV_FLAG_NO_INDEX});
    }
    nghttp2_hd_deflater* raw = nullptr;
    if (nghttp2_hd_deflate_new(&raw, 0) != 0) return fail(host_error::other);
    const std::unique_ptr<nghttp2_hd_deflater, decltype(&nghttp2_hd_deflate_del)> state(raw, nghttp2_hd_deflate_del);
    const auto bound = nghttp2_hd_deflate_bound(raw, names.data(), names.size());
    if (bound > maximum_bytes) return fail(host_error::too_large);
    std::string block(bound, '\0');
    const auto written = nghttp2_hd_deflate_hd2(raw, reinterpret_cast<std::uint8_t*>(block.data()), block.size(),
                                              names.data(), names.size());
    if (written < 0) return fail(host_error::invalid_input);
    block.resize(static_cast<std::size_t>(written));
    return HostCompletion::ok(bytes_value(std::move(block)));
}

HostCompletion decode(const std::string& block) {
    if (block.size() > maximum_bytes) return fail(host_error::too_large);
    nghttp2_hd_inflater* raw = nullptr;
    if (nghttp2_hd_inflate_new(&raw) != 0) return fail(host_error::other);
    const std::unique_ptr<nghttp2_hd_inflater, decltype(&nghttp2_hd_inflate_del)> state(raw, nghttp2_hd_inflate_del);
    std::vector<HostValue> fields;
    std::size_t offset = 0;
    std::size_t size = 0;
    for (;;) {
        nghttp2_nv field{};
        int flags = 0;
        const auto used = nghttp2_hd_inflate_hd3(raw, &field, &flags,
            reinterpret_cast<const std::uint8_t*>(block.data()) + offset, block.size() - offset, 1);
        if (used < 0 || static_cast<std::size_t>(used) > block.size() - offset) return fail(host_error::invalid_input);
        offset += static_cast<std::size_t>(used);
        if (flags & NGHTTP2_HD_INFLATE_EMIT) {
            if (fields.size() / 2 >= maximum_fields || field.namelen > maximum_bytes - size)
                return fail(host_error::too_large);
            size += field.namelen;
            if (field.valuelen > maximum_bytes - size) return fail(host_error::too_large);
            size += field.valuelen;
            std::string name(field.namelen ? reinterpret_cast<const char*>(field.name) : "", field.namelen);
            std::string value(field.valuelen ? reinterpret_cast<const char*>(field.value) : "", field.valuelen);
            if (!field_name(name) || !field_value(value)) return fail(host_error::invalid_input);
            fields.push_back(bytes_value(std::move(name)));
            fields.push_back(bytes_value(std::move(value)));
        }
        if (flags & NGHTTP2_HD_INFLATE_FINAL) {
            if (offset != block.size()) return fail(host_error::invalid_input);
            nghttp2_hd_inflate_end_headers(raw);
            return HostCompletion::ok(tuple_value(std::move(fields)));
        }
        if (used == 0 && !(flags & NGHTTP2_HD_INFLATE_EMIT)) return fail(host_error::invalid_input);
    }
}
#endif

} // namespace

void add_hpack_services(const Adder& add) {
    add(signature("neura.http.hpack.encode", {fields_type()}, byte_list()), [](const HostRequest& request) {
#ifdef NEURA_NVM_HTTP2
        return encode(request.arguments.at(0));
#else
        (void)request;
        return fail(host_error::unsupported);
#endif
    });
    add(signature("neura.http.hpack.decode", {byte_list()}, fields_type()), [](const HostRequest& request) {
#ifdef NEURA_NVM_HTTP2
        return decode(request.arguments.at(0).bytes);
#else
        (void)request;
        return fail(host_error::unsupported);
#endif
    });
}

} // namespace neura::nvm::provider
