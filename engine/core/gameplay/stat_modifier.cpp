#include "stat_modifier.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cmath>

namespace schizo::gameplay {

class DefaultModifierManager : public ModifierManager {
private:
    std::vector<std::shared_ptr<ModifierSource>> modifier_sources_;
    std::shared_ptr<spdlog::logger> logger_;
    bool debug_mode_ = false;
    
public:
    DefaultModifierManager() {
        logger_ = spdlog::get("engine");
    }
    
    void AddModifierSource(const std::shared_ptr<ModifierSource>& source) override {
        modifier_sources_.push_back(source);
        logger_->debug("Added modifier source: {}", source->name);
    }
    
    void RemoveModifierSource(const std::string& source_name) override {
        auto it = std::find_if(modifier_sources_.begin(), modifier_sources_.end(),
            [&source_name](const auto& src) { return src->name == source_name; });
        
        if (it != modifier_sources_.end()) {
            modifier_sources_.erase(it);
            logger_->debug("Removed modifier source: {}", source_name);
        }
    }
    
    void Update(float delta_time) override {
        // Update temporary modifiers
        std::vector<std::string> expired_sources;
        
        for (auto& source : modifier_sources_) {
            if (source->IsExpired(delta_time)) {
                expired_sources.push_back(source->name);
            }
        }
        
        // Clean up expired sources
        for (const auto& name : expired_sources) {
            RemoveModifierSource(name);
        }
    }
    
    AggregatedStat GetAggregatedStat(StatType stat, float base_value) override {
        AggregatedStat aggregated;
        aggregated.stat = stat;
        aggregated.base_value = base_value;
        
        // Aggregate all modifiers
        for (const auto& source : modifier_sources_) {
            if (!source) continue;
            
            for (const auto& modifier : source->modifiers) {
                if (!modifier.active || modifier.stat != stat) continue;
                
                AggregatedStat::Breakdown breakdown;
                breakdown.source = modifier.source;
                
                if (modifier.type == ModifierType::Additive) {
                    aggregated.additive_bonus += modifier.value;
                    breakdown.additive_contribution = modifier.value;
                } else if (modifier.type == ModifierType::Multiplicative) {
                    aggregated.multiplicative_bonus *= modifier.value;
                    breakdown.multiplicative_contribution = modifier.value;
                }
                
                aggregated.breakdown.push_back(breakdown);
            }
        }
        
        if (debug_mode_) {
            LogBreakdown(aggregated);
        }
        
        return aggregated;
    }
    
    float CalculateStat(StatType stat, float base_value) override {
        auto aggregated = GetAggregatedStat(stat, base_value);
        return aggregated.GetFinalValue();
    }
    
private:
    void LogBreakdown(const AggregatedStat& aggregated) {
        logger_->debug("Stat aggregation ({}): base={}, additive+={}, mult×={}",
            static_cast<int>(aggregated.stat),
            aggregated.base_value,
            aggregated.additive_bonus,
            aggregated.multiplicative_bonus);
        
        for (const auto& b : aggregated.breakdown) {
            if (b.additive_contribution != 0.0f) {
                logger_->debug("  ├─ (add) {}: +{}", b.source, b.additive_contribution);
            }
            if (b.multiplicative_contribution != 1.0f) {
                logger_->debug("  └─ (mul) {}: ×{}", b.source, b.multiplicative_contribution);
            }
        }
    }
};

std::shared_ptr<ModifierManager> ModifierManager::Create() {
    return std::make_shared<DefaultModifierManager>();
}

} // namespace schizo::gameplay
