#pragma once

#include "effects.hpp"
#include "neura-ir/purity.hpp"

#include <memory>

namespace neura::optimization {

// The purity and effect analyses of a program as it stands between
// rewrites. Analyses depend only on the program, so while no pass rewrites
// it every pass can share one copy of each: optimize() installs a cache for
// its run and clears it whenever a pass reports a rewrite. Without an
// installed cache, as when a test runs one pass on its own, each Analyses
// builds its own.
class AnalysisCache {
public:
    AnalysisCache();
    ~AnalysisCache();
    AnalysisCache(const AnalysisCache&) = delete;
    AnalysisCache& operator=(const AnalysisCache&) = delete;

    // Forgets the analyses after the program has changed.
    void clear();

private:
    friend class Analyses;
    AnalysisCache* previous_;
    const IrProgram* program_ = nullptr;
    std::unique_ptr<PurityAnalysis> purity_;
    std::unique_ptr<EffectAnalysis> effects_;
};

// The analyses one pass reads: the installed cache's, or its own.
class Analyses {
public:
    explicit Analyses(const IrProgram& program);
    const PurityAnalysis& purity();
    const EffectAnalysis& effects();

private:
    const IrProgram& program_;
    AnalysisCache* cache_;
    std::unique_ptr<PurityAnalysis> purity_;
    std::unique_ptr<EffectAnalysis> effects_;
};

} // namespace neura::optimization
