#include "nvm/hpack_provider.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>

using namespace neura::nvm;
using namespace neura::nvm::provider;

int main() {
    std::map<std::string, FunctionService::Handler> services;
    add_hpack_services([&](HostSignature signature, FunctionService::Handler handler) {
        services.emplace(std::move(signature.name), std::move(handler));
    });
    const auto invoke = [&](const char* name, HostValue input) {
        HostRequest request;
        request.arguments.push_back(std::move(input));
        return services.at(name)(request);
    };
    const auto decompress = [&](const std::string& bytes) {
        return invoke("neura.http.hpack.decode", bytes_value(bytes));
    };
    const auto compress = [&](const HostValue& fields) { return invoke("neura.http.hpack.encode", fields); };
#ifndef NEURA_NVM_HTTP2
    assert(!decompress("").success && decompress("").value.bits == host_error::unsupported);
    assert(!compress(tuple_value({})).success && compress(tuple_value({})).value.bits == host_error::unsupported);
    std::cout << "PASS HPACK-disabled typed Unsupported\n";
#else
    const auto fields = [](std::initializer_list<std::pair<std::string, std::string>> pairs) {
        std::vector<HostValue> flat;
        for (const auto& [name, value] : pairs) {
            flat.push_back(bytes_value(name));
            flat.push_back(bytes_value(value));
        }
        return tuple_value(std::move(flat));
    };
    // RFC 7541 C.3.1 and C.4.1: the same request without/with Huffman strings.
    const auto expected = fields({{":method", "GET"}, {":scheme", "http"}, {":path", "/"},
                                  {":authority", "www.example.com"}});
    const std::string literal = std::string("\x82\x86\x84\x41\x0f", 5) + "www.example.com";
    const std::string huffman("\x82\x86\x84\x41\x8c\xf1\xe3\xc2\xe5\xf2\x3a\x6b\xa0\xab\x90\xf4\xff", 17);
    for (const auto& block : {literal, huffman}) {
        const auto decoded = decompress(block);
        assert(decoded.success && decoded.value == expected);
    }
    const auto encoded = compress(expected);
    assert(encoded.success);
    assert(decompress(encoded.value.bytes).value == expected);
    assert(compress(expected).value.bytes == encoded.value.bytes);
    assert(decompress("").success && decompress("").value.elements.empty());
    std::cout << "PASS RFC 7541 literal/Huffman pseudo-fields, independent deterministic encoding, empty block\n";

    // A dynamic reference inside one block succeeds; across independent calls
    // the table is fresh. This API is intentionally not a connection codec.
    const auto dynamic = decompress(std::string("\x40\x01x\x01y\xbe", 6));
    assert(dynamic.success && dynamic.value == fields({{"x", "y"}, {"x", "y"}}));
    assert(!decompress(std::string("\xbe", 1)).success);
    for (const auto& block : {std::string("\x80", 1), std::string("\xff", 1),
                              std::string("\x40\0\0", 3), std::string("\0\x01x\x81\xff", 5)}) {
        const auto invalid = decompress(block);
        assert(!invalid.success && invalid.value.bits == host_error::invalid_input);
    }
    assert(!compress(fields({{"Upper", "value"}})).success);
    assert(!compress(fields({{"x", "bad\r\nvalue"}})).success);
    std::cout << "PASS HPACK malformed/truncated/Huffman rejection and fresh table boundary\n";

    const auto many = decompress(std::string(257, static_cast<char>(0x82)));
    assert(!many.success && many.value.bits == host_error::too_large);
    const auto large_block = decompress(std::string(65537, '\0'));
    assert(!large_block.success && large_block.value.bits == host_error::too_large);
    const auto odd = compress(tuple_value({bytes_value("unpaired-name")}));
    assert(!odd.success && odd.value.bits == host_error::invalid_input);
    const auto large_field = tuple_value({bytes_value("x"), bytes_value(std::string(65536, 'v'))});
    const auto large = compress(large_field);
    assert(!large.success && large.value.bits == host_error::too_large);
    std::cout << "PASS HPACK field count, encoded bytes and decoded bytes limits\n";
#endif
}
