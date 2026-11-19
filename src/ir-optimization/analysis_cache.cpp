#include "analysis_cache.hpp"

namespace neura::optimization {

namespace {

thread_local AnalysisCache* installed = nullptr;

} // namespace

AnalysisCache::AnalysisCache() : previous_(installed) { installed = this; }

AnalysisCache::~AnalysisCache() { installed = previous_; }

void AnalysisCache::clear() {
    effects_.reset();
    purity_.reset();
    program_ = nullptr;
}

Analyses::Analyses(const IrProgram& program) : program_(program), cache_(installed) {
    // A cache built for another program is not this program's.
    if (cache_ && cache_->program_ && cache_->program_ != &program) cache_->clear();
    if (cache_) cache_->program_ = &program;
}

const PurityAnalysis& Analyses::purity() {
    auto& slot = cache_ ? cache_->purity_ : purity_;
    if (!slot) slot = std::make_unique<PurityAnalysis>(program_);
    return *slot;
}

const EffectAnalysis& Analyses::effects() {
    const auto& purity_analysis = purity();
    auto& slot = cache_ ? cache_->effects_ : effects_;
    if (!slot) slot = std::make_unique<EffectAnalysis>(program_, purity_analysis);
    return *slot;
}

} // namespace neura::optimization
