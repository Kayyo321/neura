#include "inspect.hpp"
#include "json.hpp"
#include "nvr/format.hpp"
#include "nvr/names.hpp"
#include "runner/runner.hpp"

#include <algorithm>
#include <charconv>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>

namespace neura::tooling {
int run_image_inspector(const std::vector<std::string>& arguments) {
    std::filesystem::path file;
    std::optional<std::size_t> selected;
    std::size_t limit = 1000;
    bool json = false;
    const auto number = [](const std::string& text) {
        std::size_t value{};
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) throw std::runtime_error("expected a nonnegative integer");
        return value;
    };
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const auto& arg = arguments[i];
        if (arg == "--json") json = true;
        else if (arg == "--graph" || arg == "--limit") {
            if (++i == arguments.size()) throw std::runtime_error(arg + " needs a value");
            if (arg == "--graph") selected = number(arguments[i]); else limit = number(arguments[i]);
        } else if (!arg.empty() && arg.front() == '-') throw std::runtime_error("unknown inspect-image option: " + arg);
        else if (file.empty()) file = arg;
        else throw std::runtime_error("inspect-image takes one .nvr file");
    }
    if (!json || file.empty() || limit == 0 || limit > 10000) throw std::runtime_error("usage: neura inspect-image --json <image.nvr> [--graph N] [--limit 1..10000]");
    const auto image = nvr::load(runner::read_file(file));
    const auto names = nvr::decode_names(image);
    Json result{{"version", 1}, {"format", "NVR"}, {"minorVersion", image.minor_version}, {"entry", image.entry},
                {"path", std::filesystem::absolute(file).generic_string()}};
    Json graphs = Json::Array{}, functions = Json::Array{}, imports = Json::Array{}, sources = Json::Array{};
    std::set<std::string> source_names;
    std::map<std::pair<std::uint32_t, std::uint32_t>, nvr::SourceLocation> locations;
    if (image.source_map) for (const auto& location : *image.source_map) {
        locations[{location.function, location.block}] = location;
        if (source_names.insert(location.path).second) sources.push_back(location.path);
    }
    for (std::size_t g = 0; g < image.graphs.size(); ++g) {
        const auto& graph = image.graphs[g]; std::size_t edges = 0;
        for (const auto& node : graph.nodes) edges += node.edges.size();
        graphs.push_back(Json{{"id", g}, {"neurons", graph.nodes.size()}, {"edges", edges}, {"inputLanes", graph.input_lanes}, {"outputLanes", graph.outputs.size()}});
    }
    for (std::size_t f = 0; f < image.functions.size(); ++f) {
        const auto& function = image.functions[f]; Json blocks = Json::Array{};
        for (std::size_t b = 0; b < function.blocks.size(); ++b) {
            Json block{{"id", b}, {"graph", function.blocks[b].graph}};
            const auto found = locations.find({static_cast<std::uint32_t>(f), static_cast<std::uint32_t>(b)});
            if (found != locations.end()) block["source"] = Json{{"path", found->second.path}, {"line", found->second.line}, {"column", found->second.column}};
            blocks.push_back(std::move(block));
        }
        const auto name = names && f < names->functions.size() ? names->functions[f] : "f" + std::to_string(f);
        functions.push_back(Json{{"id", f}, {"name", name}, {"entry", 0}, {"blocks", std::move(blocks)}});
    }
    for (const auto& import : image.imports) imports.push_back(Json{{"name", import.name}});
    result["graphs"] = std::move(graphs); result["functions"] = std::move(functions);
    result["imports"] = std::move(imports); result["sources"] = std::move(sources);
    if (selected) {
        if (*selected >= image.graphs.size()) throw std::runtime_error("graph index is outside the image");
        const auto& graph = image.graphs[*selected]; Json neurons = Json::Array{}, edges = Json::Array{}, outputs = Json::Array{};
        std::size_t total_edges = 0;
        for (std::size_t n = 0; n < graph.nodes.size(); ++n) {
            const auto& node = graph.nodes[n]; total_edges += node.edges.size();
            if (n >= limit) continue;
            const auto id = graph.input_lanes + n;
            neurons.push_back(Json{{"id", n}, {"wire", id}, {"bias", std::to_string(node.bias)}, {"activation", node.activation == nvr::Activation::Relu ? "relu" : "identity"}});
            for (const auto& edge : node.edges) if (edges.items().size() < limit * 8)
                edges.push_back(Json{{"from", edge.source}, {"to", id}, {"weight", std::to_string(edge.weight)}});
        }
        for (const auto output : graph.outputs) outputs.push_back(output);
        result["graph"] = Json{{"id", *selected}, {"inputLanes", graph.input_lanes}, {"neurons", std::move(neurons)}, {"edges", std::move(edges)}, {"outputs", std::move(outputs)},
                                {"truncated", graph.nodes.size() > limit || total_edges > limit * 8}, {"totalNeurons", graph.nodes.size()}, {"totalEdges", total_edges}};
    }
    std::cout << result.dump() << '\n'; return 0;
}
} // namespace neura::tooling
