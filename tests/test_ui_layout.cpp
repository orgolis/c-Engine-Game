#include <catch2/catch_test_macros.hpp>

#include "ui_layout.h"

namespace ui = schizo::editor::ui;

namespace {
struct UiFrame {
    ImGuiContext* previous = ImGui::GetCurrentContext();
    ImGuiContext* context = ImGui::CreateContext();
    explicit UiFrame(float width, float scale) {
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.DisplaySize = ImVec2(1600, 1200);
        io.DeltaTime = 1.0f / 60.0f;
        unsigned char* pixels = nullptr;
        int w = 0, h = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
        ImGui::GetStyle().WindowPadding = ImVec2(12, 12);
        ImGui::GetStyle().FramePadding = ImVec2(8, 6);
        ImGui::NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width, 1100));
        ImGui::Begin("Layout check", nullptr, ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(scale);
    }
    ~UiFrame() {
        ImGui::End();
        ImGui::EndFrame();
        ImGui::DestroyContext(context);
        ImGui::SetCurrentContext(previous);
    }
};

void stays_inside(float right) {
    CHECK(ImGui::GetItemRectMax().x <= right + 1.0f);
    CHECK(ImGui::GetItemRectMin().x >= ImGui::GetWindowPos().x);
}
}  // namespace

TEST_CASE("Property fields stay inside narrow panels at different font scales", "[editor][ui-layout]") {
    for (float width : {190.0f, 280.0f, 440.0f, 900.0f}) {
        for (float scale : {1.0f, 1.5f}) {
            CAPTURE(width, scale);
            UiFrame frame(width, scale);
            const float right = ImGui::GetCursorScreenPos().x + ui::AvailableWidth();
            char path[260] = "assets/skies/test.hdr";
            float intensity = 1.0f;
            float xyz[3] = {1, 2, 3};
            float color[4] = {1, 1, 1, 1};
            int option = 0;
            const char* options[] = {"Low", "High"};

            const bool action_wraps = ui::AvailableWidth() - ui::ButtonWidth("Clear") -
                ImGui::GetStyle().ItemSpacing.x < ImGui::GetFontSize() * 8.0f;
            ui::InputTextWithAction("Sky HDR", "Clear", path, sizeof(path));
            stays_inside(right);
            const float field_y = ImGui::GetItemRectMin().y;
            ui::SameLineIfFits("Clear");
            ui::Button("Clear");
            stays_inside(right);
            if (action_wraps) CHECK(ImGui::GetItemRectMin().y > field_y);
            else CHECK(ImGui::GetItemRectMin().y == field_y);
            ui::DragFloat("Sky Intensity", &intensity, 0.02f);
            stays_inside(right);
            ui::DragFloat3("A longer transform field label", xyz);
            stays_inside(right);
            ui::ColorEdit4("Base Color", color);
            stays_inside(right);
            ui::Combo("Ground Check Distance##test", &option, options, 2);
            stays_inside(right);
            ui::SetNextItemWidth(1000.0f);
            ui::InputText("##anonymous", path, sizeof(path));
            stays_inside(right);
            ui::TextDisabledWrapped("Drag an .hdr here, or type a project-relative path.");
            stays_inside(right);
            bool enabled = true;
            const ImGuiID checkbox = ImGui::GetID("A very long checkbox label that must wrap##check");
            ui::Checkbox("A very long checkbox label that must wrap##check", &enabled);
            stays_inside(right);
            CHECK(ImGui::GetItemID() == checkbox);
        }
    }
}

TEST_CASE("Moving field labels preserves widget IDs and item edit metadata", "[editor][ui-layout]") {
    UiFrame frame(440.0f, 1.0f);
    char value[64] = "unchanged";
    for (const char* label : {"Sky HDR", "Clip##audiosrc", "##anonymous", "Value###stable"}) {
        CAPTURE(label);
        const ImGuiID original = ImGui::GetID(label);
        ui::InputText(label, value, sizeof(value));
        CHECK(ImGui::GetItemID() == original);
        CHECK_FALSE(ImGui::IsItemEdited());
        CHECK_FALSE(ImGui::IsItemDeactivatedAfterEdit());
    }
}

TEST_CASE("Toolbar buttons wrap instead of extending docked window content", "[editor][ui-layout]") {
    for (float width : {190.0f, 280.0f, 440.0f, 900.0f}) {
        UiFrame frame(width, 1.5f);
        const float right = ImGui::GetCursorScreenPos().x + ui::AvailableWidth();
        bool first = true;
        for (const char* label : {"Assets", "New...", "Hide Tree", "Refresh", "Import..."}) {
            if (!first) ui::SameLineIfFits(label);
            ui::Button(label);
            stays_inside(right);
            first = false;
        }
        ui::Button("A deliberately oversized button", ImVec2(500, 0));
        stays_inside(right);
    }
}
