#pragma once
// Spec 19 §1 typography and §7 HiDPI — the pinned font set. Inter for UI text, JetBrains Mono for
// code and readouts; no ImGui default font is ever loaded (spec 19 §1, §9). Every face is
// rasterised at `sizePx × dpiScale × fontScale` with FreeType hinting off and 3×1 oversampling, and
// the whole set is rebuilt when the DPI or the user font scale changes.
#include "Core/Error.hpp"
#include "UI/Theme.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <imgui.h>

namespace qlab::ui {

enum class FontRole : std::uint8_t {
    Body,        // Inter Regular 13
    Secondary,   // Inter Regular 12 (labels, units)
    Strong,      // Inter SemiBold 13
    PanelTitle,  // Inter SemiBold 15
    Workspace,   // Inter SemiBold 20
    Code,        // JetBrains Mono 13 (editor)
    CodeSmall,   // JetBrains Mono 12 (log)
    Readout,     // JetBrains Mono 16, tabular figures (instrument screens)
    Count
};
inline constexpr std::size_t kFontRoleCount = static_cast<std::size_t>(FontRole::Count);
std::string_view fontRoleName(FontRole r);

struct FontSet {
    std::array<ImFont*, kFontRoleCount> faces{};
    std::array<float, kFontRoleCount> sizes{};   // rasterised pixel size
    float dpiScale = 1.0f, fontScale = 1.0f;
    bool loaded = false;                         // false: the atlas holds no QuantumXLab face

    ImFont* get(FontRole r) const { return faces[static_cast<std::size_t>(r)]; }
    float size(FontRole r) const { return sizes[static_cast<std::size_t>(r)]; }

    // Clears `atlas` and rasterises the whole set from Assets/Fonts. Needs an ImGui context only
    // insofar as `atlas` must be alive; it does NOT need a GL context (the texture is uploaded by
    // the renderer backend later). Fails with err::NoFont when a pinned file is missing.
    static Result<FontSet> build(ImFontAtlas* atlas, const Theme& theme, float dpiScale = 1.0f,
                                 float fontScale = 1.0f, const std::filesystem::path& fontDir = {});
    // Assets/Fonts, or QXL_FONT_DIR when set.
    static std::filesystem::path defaultDir();
    // True when every pinned face exists on disk (tests SKIP otherwise).
    static bool available(const std::filesystem::path& fontDir = {});
};

// RAII push of a role for the enclosed widgets.
class FontScope {
public:
    FontScope(const FontSet& set, FontRole role) : pushed_(set.get(role) != nullptr) {
        if (pushed_) ImGui::PushFont(set.get(role));
    }
    ~FontScope() {
        if (pushed_) ImGui::PopFont();
    }
    FontScope(const FontScope&) = delete;
    FontScope& operator=(const FontScope&) = delete;

private:
    bool pushed_;
};

} // namespace qlab::ui
