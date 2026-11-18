#pragma once

#include "ir.hpp"

namespace neura {

// A concrete, acyclic JSON schema. A slot refers to another shape by its
// index, including when the same record occurs in several members.
struct CodecSlot {
    SymbolId symbol = no_symbol;
    std::string name;
    std::string wire;
    std::size_t shape{};
    bool optional{};
};

struct CodecVariant {
    SymbolId symbol = no_symbol;
    std::string name;
    std::string wire;
    bool named{};
    std::vector<CodecSlot> slots;
};

struct CodecShape {
    IrType type;
    std::string spelling;
    std::vector<CodecSlot> slots;
    std::vector<CodecVariant> variants;
};

// Ordinary Neura declarations. The lowering pass binds the type/module
// names in these declarations to the symbols that produced the schema.
std::string json_codec_declaration(std::string_view module, const std::vector<CodecShape>& shapes);

}
