#pragma once

#include "analysis.hpp"

#include <string_view>

namespace neura::tooling {

// Edits are versioned, user-invoked actions. Every proposed source change is
// rechecked through an isolated compiler session before it is offered.
Json refactor_actions(AnalysisSession& session, const EditorDocument& document, const Json& params);

// Standard LSP call and type hierarchy methods, using resolved IR edges.
Json hierarchy_request(AnalysisSession& session, const EditorDocument& document,
                       std::string_view method, const Json& params);

} // namespace neura::tooling
