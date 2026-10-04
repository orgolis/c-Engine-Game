#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace schizo::editor {

struct AiModelOption {
    std::string id;
    std::string label;
    std::vector<std::string> reasoning_efforts;
    std::string default_reasoning_effort;
    bool is_default = false;
};

inline const AiModelOption* RecommendedAiModel(const std::vector<AiModelOption>& models) {
    const auto recommended = std::find_if(models.begin(), models.end(),
        [](const AiModelOption& model) { return model.is_default; });
    if (recommended != models.end()) return &*recommended;
    return models.empty() ? nullptr : &models.front();
}

inline const AiModelOption* FindAiModel(const std::vector<AiModelOption>& models, const std::string& id) {
    if (id.empty()) return RecommendedAiModel(models);
    const auto selected = std::find_if(models.begin(), models.end(),
        [&](const AiModelOption& model) { return model.id == id; });
    return selected == models.end() ? nullptr : &*selected;
}

inline std::string SupportedAiReasoningEffort(const AiModelOption* model, const std::string& preferred) {
    if (!model || model->reasoning_efforts.empty()) return {};
    const auto supported = [&](const std::string& effort) {
        return std::find(model->reasoning_efforts.begin(), model->reasoning_efforts.end(), effort) !=
               model->reasoning_efforts.end();
    };
    if (supported(preferred)) return preferred;
    if (supported(model->default_reasoning_effort)) return model->default_reasoning_effort;
    return model->reasoning_efforts.front();
}

}  // namespace schizo::editor
