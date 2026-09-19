#pragma once
// INTERNAL to src/Viz: included by this module's .cpp files only, never by a public header and
// never through Viz.hpp (ImGui stays out of the module's interface; spec 19 lint allows ImGui
// calls in src/UI, src/Viz and src/App).
#include "Viz/IStateView.hpp"
#include <imgui.h>
#include <string>
#include <string_view>

namespace qlab::viz::detail {

inline ImU32 toU32(const glm::vec4& c) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a));
}
inline ImU32 toU32(const glm::vec3& c, float alpha = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, alpha));
}
inline ImVec4 toImVec4(const glm::vec4& c) {
    return ImVec4(c.r, c.g, c.b, c.a);
}
inline ImVec2 toImVec2(glm::vec2 v) {
    return ImVec2(v.x, v.y);
}
inline glm::vec2 toGlm(ImVec2 v) {
    return {v.x, v.y};
}

// ImGui text helpers over string_view (ImGui wants a begin/end pair, not a terminator).
inline void text(const glm::vec4& color, std::string_view s) {
    ImGui::PushStyleColor(ImGuiCol_Text, toImVec4(color));
    ImGui::TextUnformatted(s.data(), s.data() + s.size());
    ImGui::PopStyleColor();
}
inline ImVec2 textSize(std::string_view s) {
    return ImGui::CalcTextSize(s.data(), s.data() + s.size());
}
inline void drawText(ImDrawList* dl, ImVec2 pos, const glm::vec4& color, std::string_view s) {
    dl->AddText(pos, toU32(color), s.data(), s.data() + s.size());
}
// Text centred on `center`.
inline void drawTextCentered(ImDrawList* dl, ImVec2 center, const glm::vec4& color,
                             std::string_view s) {
    const ImVec2 sz = textSize(s);
    drawText(dl, ImVec2(center.x - 0.5f * sz.x, center.y - 0.5f * sz.y), color, s);
}

} // namespace qlab::viz::detail
