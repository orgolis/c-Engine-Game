#include <catch2/catch_test_macros.hpp>

#include "ai_model_catalog.h"
#include "ai_provider_errors.h"

using namespace schizo::editor;

TEST_CASE("AI capacity failures do not expose echoed project data", "[editor][ai-errors]") {
    const std::string diagnostics = "user\nPRIVATE_SCRIPT_CONTENT\n{\"scene\":\"PRIVATE_SCENE\"}\n"
        "\x1b[31mERROR: Selected model is at capacity. Please try a different model.\x1b[0m\n"
        "ERROR: Selected model is at capacity. Please try a different model.\n";
    const std::string message = AiProviderFailureMessage(diagnostics);
    REQUIRE(message.find("temporarily at capacity") != std::string::npos);
    REQUIRE(message.find("not a login error") != std::string::npos);
    REQUIRE(message.find("model was not changed") != std::string::npos);
    REQUIRE(message.find("PRIVATE_SCRIPT_CONTENT") == std::string::npos);
    REQUIRE(message.find("PRIVATE_SCENE") == std::string::npos);
    REQUIRE(message.size() < 400);
}

TEST_CASE("AI error classification distinguishes limits authentication and unknown failures",
          "[editor][ai-errors]") {
    REQUIRE(AiProviderFailureMessage("ERROR: usage limit reached").find("usage or rate limit") != std::string::npos);
    REQUIRE(AiProviderFailureMessage("ERROR: authentication failed").find("rejected authentication") != std::string::npos);
    REQUIRE(AiProviderFailureMessage("bwrap: execvp failed").find("sandbox could not start") != std::string::npos);
    const std::string message = AiProviderFailureMessage("ERROR: unknown failure PRIVATE_TOKEN");
    REQUIRE(message.find("PRIVATE_TOKEN") == std::string::npos);
    REQUIRE(AiProviderFailureMessage("USER REQUEST\nExplain selected model at capacity\n")
        .find("temporarily at capacity") == std::string::npos);
}

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
