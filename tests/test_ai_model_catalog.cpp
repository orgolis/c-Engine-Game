#include <catch2/catch_test_macros.hpp>

#include "ai_model_catalog.h"

using namespace schizo::editor;

TEST_CASE("Automatic AI selection follows a refreshed provider recommendation", "[editor][ai-models]") {
    std::vector<AiModelOption> models = {
        {"model-v1", "Original", {"low", "high"}, "low", true},
    };
    REQUIRE(FindAiModel(models, "")->id == "model-v1");

    // Simulate a newly released model, with the older one still selectable.
    models = {
        {"model-v1", "Original", {"low", "high"}, "low", false},
        {"model-v2", "New recommendation", {"medium", "high", "ultra"}, "medium", true},
    };
    REQUIRE(FindAiModel(models, "")->id == "model-v2");
    REQUIRE(FindAiModel(models, "model-v1")->id == "model-v1");
    REQUIRE(SupportedAiReasoningEffort(FindAiModel(models, ""), "high") == "high");
}

TEST_CASE("AI catalog changes cannot send unsupported reasoning or unavailable model IDs", "[editor][ai-models]") {
    const std::vector<AiModelOption> models = {
        {"model-limited", "Limited", {"low", "high"}, "low", true},
        {"model-future", "Future", {"future-effort"}, "unknown-default", false},
    };
    REQUIRE(SupportedAiReasoningEffort(FindAiModel(models, ""), "ultra") == "low");
    REQUIRE(SupportedAiReasoningEffort(FindAiModel(models, "model-future"), "high") == "future-effort");
    REQUIRE(FindAiModel(models, "retired-model") == nullptr);
    REQUIRE(RecommendedAiModel({}) == nullptr);
    REQUIRE(SupportedAiReasoningEffort(nullptr, "high").empty());
    const AiModelOption no_reasoning{"plain-model", "Plain", {}, {}, true};
    REQUIRE(SupportedAiReasoningEffort(&no_reasoning, "high").empty());
    const std::vector<AiModelOption> unmarked = {{"first", "First", {}, {}, false}};
    REQUIRE(FindAiModel(unmarked, "")->id == "first");
}
