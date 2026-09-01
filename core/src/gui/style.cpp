#include <gui/style.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <config.h>
#include <utils/flog.h>
#include <filesystem>

static inline std::filesystem::path toFsPath(const std::string& utf8Str) {
#if defined(_WIN32)
    return std::filesystem::u8path(utf8Str);
#else
    return std::filesystem::path(utf8Str);
#endif
}

namespace style {
    ImFont* baseFont;
    ImFont* bigFont;
    ImFont* hugeFont;
    ImVector<ImWchar> baseRanges;
    ImVector<ImWchar> bigRanges;
    ImVector<ImWchar> hugeRanges;

#ifndef __ANDROID__
    float uiScale = 1.0f;
#else
    float uiScale = 3.0f;
#endif

    bool loadFonts(std::string resDir) {
        ImFontAtlas* fonts = ImGui::GetIO().Fonts;
        if (!std::filesystem::is_directory(toFsPath(resDir))) {
            flog::error("Invalid resource directory: {0}", resDir);
            return false;
        }

        // Create base font range
        ImFontGlyphRangesBuilder baseBuilder;
        baseBuilder.AddRanges(fonts->GetGlyphRangesDefault());
        baseBuilder.AddRanges(fonts->GetGlyphRangesCyrillic());
        baseBuilder.BuildRanges(&baseRanges);

        // Create big font range
        ImFontGlyphRangesBuilder bigBuilder;
        const ImWchar bigRange[] = { '.', '9', 0 };
        bigBuilder.AddRanges(bigRange);
        bigBuilder.BuildRanges(&bigRanges);

        // Create huge font range
        ImFontGlyphRangesBuilder hugeBuilder;
        const ImWchar hugeRange[] = { 'S', 'S', 'D', 'D', 'R', 'R', '+', '+', ' ', ' ', 0 };
        hugeBuilder.AddRanges(hugeRange);
        hugeBuilder.BuildRanges(&hugeRanges);
        
        // 1. Add base Western font
        std::string robotoPath = resDir + "/fonts/Roboto-Medium.ttf";
        baseFont = fonts->AddFontFromFileTTF(robotoPath.c_str(), 16.0f * uiScale, NULL, baseRanges.Data);

        // 2. Merge Chinese / CJK Font into baseFont so Chinese characters display perfectly
        ImFontConfig mergeConfig;
        mergeConfig.MergeMode = true;
        mergeConfig.PixelSnapH = true;

        const char* cjkCandidates[] = {
            "C:/Windows/Fonts/msyh.ttc",   // Microsoft YaHei (Windows)
            "C:/Windows/Fonts/simhei.ttf", // SimHei (Windows)
            "C:/Windows/Fonts/simsun.ttc", // SimSun (Windows)
            "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
            "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
            "/System/Library/Fonts/PingFang.ttc"
        };

        for (const char* cjkPath : cjkCandidates) {
            try {
                if (std::filesystem::is_regular_file(toFsPath(cjkPath))) {
                    fonts->AddFontFromFileTTF(cjkPath, 16.0f * uiScale, &mergeConfig, fonts->GetGlyphRangesChineseFull());
                    flog::info("Successfully loaded CJK font for Chinese language: {0}", cjkPath);
                    break;
                }
            } catch (...) {}
        }

        // Add bigger fonts for frequency select and title
        bigFont = fonts->AddFontFromFileTTF(robotoPath.c_str(), 45.0f * uiScale, NULL, bigRanges.Data);
        hugeFont = fonts->AddFontFromFileTTF(robotoPath.c_str(), 128.0f * uiScale, NULL, hugeRanges.Data);

        return true;
    }

    void beginDisabled() {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        auto& style = ImGui::GetStyle();
        ImVec4* colors = style.Colors;
        ImVec4 btnCol = colors[ImGuiCol_Button];
        ImVec4 frameCol = colors[ImGuiCol_FrameBg];
        ImVec4 textCol = colors[ImGuiCol_Text];
        btnCol.w = 0.15f;
        frameCol.w = 0.30f;
        textCol.w = 0.65f;
        ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, frameCol);
        ImGui::PushStyleColor(ImGuiCol_Text, textCol);
    }

    void endDisabled() {
        ImGui::PopItemFlag();
        ImGui::PopStyleColor(3);
    }
}

namespace ImGui {
    void LeftLabel(const char* text) {
        float vpos = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(vpos + GImGui->Style.FramePadding.y);
        ImGui::TextUnformatted(text);
        ImGui::SameLine();
        ImGui::SetCursorPosY(vpos);
    }

    void FillWidth() {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    }
}
