#include "commands.hpp"

#include "decompiler/assembly.hpp"
#include "decompiler/decompile.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <map>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace neura::cli {

using neura::runner::read_file;
using neura::runner::write_file;

int disassemble(const Options& options) {
    write_text(options, neura::decompiler::disassemble(load_image(options.files.front())));
    return 0;
}

int decompile(const Options& options) {
    neura::decompiler::DecompileOptions decompile_options;
    decompile_options.provenance = options.provenance;
    write_text(options, neura::decompiler::decompile(load_image(options.files.front()), decompile_options).text);
    return 0;
}

int assemble(const Options& options) {
    const auto bytes = read_file(options.files.front());
    neura::nvr::Image image;
    try {
        image = neura::decompiler::assemble(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    } catch (const neura::decompiler::AssemblyError& error) {
        throw std::runtime_error(options.files.front().string() + ": " + error.what());
    }
    std::vector<std::uint8_t> encoded;
    try {
        encoded = neura::nvr::encode(image);
        // The assembled image passes the same validation as any other.
        neura::nvr::load(encoded);
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(options.files.front().string() + ": cannot encode the image: " + error.what());
    } catch (const neura::nvr::LoadError& error) {
        throw std::runtime_error(options.files.front().string() + ": the assembled image is " +
                                 std::string(neura::nvr::describe(error.kind)) + ": " + error.what());
    }
    write_file(*options.output, encoded);
    return 0;
}

} // namespace neura::cli
