// Spec 19 §4 / 23 §2 — layout persistence (see Layout.hpp).
#include "UI/Layout.hpp"
#include "UI/Theme.hpp"
#include <algorithm>

namespace qlab::ui {
namespace {
bool registered = false;
}

void registerLayoutSchema() {
    if (registered)
        return;
    core::JsonEnvelope::registerKind("ui.layout", LayoutState::kSchema);
    registered = true;
}

core::Json LayoutState::toJson() const {
    core::Json j = core::Json::object();
    j["active"] = std::string(workspaceName(active));
    j["physical_lab"] = physicalLab;
    j["palette"] = palette;
    j["font_scale"] = fontScale;
    if (!shortcuts.is_null() && !shortcuts.empty())
        j["shortcuts"] = shortcuts;

    core::Json ws = core::Json::object();
    for (std::size_t i = 0; i < kWorkspaceCount; ++i) {
        const WorkspaceLayout& w = workspaces[i];
        core::Json entry = core::Json::object();
        entry["ini"] = w.ini;
        entry["open"] = w.open;
        entry["customised"] = w.customised;
        ws[std::string(workspaceName(static_cast<Workspace>(i)))] = std::move(entry);
    }
    j["workspaces"] = std::move(ws);

    core::Json ps = core::Json::object();
    for (const auto& [key, value] : panels)
        ps[key] = value;
    j["panels"] = std::move(ps);
    return j;
}

Result<LayoutState> LayoutState::fromJson(const core::Json& j) {
    if (!j.is_object())
        return fail(err::BadLayout, "layout: `data` is not an object");
    LayoutState s;
    if (const auto it = j.find("active"); it != j.end() && it->is_string()) {
        const auto w = workspaceFromName(it->get<std::string>());
        if (!w)
            return fail(err::BadLayout,
                        "layout: unknown workspace '" + it->get<std::string>() + "'");
        s.active = *w;
    }
    if (const auto it = j.find("physical_lab"); it != j.end() && it->is_boolean())
        s.physicalLab = it->get<bool>();
    if (const auto it = j.find("palette"); it != j.end() && it->is_string())
        s.palette = it->get<std::string>();
    if (const auto it = j.find("font_scale"); it != j.end() && it->is_number())
        s.fontScale = std::clamp(it->get<float>(), 0.8f, 1.6f); // spec 19 §7
    if (const auto it = j.find("shortcuts"); it != j.end())
        s.shortcuts = *it;

    if (const auto ws = j.find("workspaces"); ws != j.end()) {
        if (!ws->is_object())
            return fail(err::BadLayout, "layout: `workspaces` is not an object");
        for (const auto& [name, entry] : ws->items()) {
            const auto w = workspaceFromName(name);
            if (!w)
                return fail(err::BadLayout, "layout: unknown workspace '" + name + "'");
            WorkspaceLayout& into = s.of(*w);
            if (!entry.is_object())
                return fail(err::BadLayout, "layout: workspace '" + name + "' is not an object");
            if (const auto i = entry.find("ini"); i != entry.end() && i->is_string())
                into.ini = i->get<std::string>();
            if (const auto i = entry.find("customised"); i != entry.end() && i->is_boolean())
                into.customised = i->get<bool>();
            if (const auto i = entry.find("open"); i != entry.end() && i->is_array())
                for (const core::Json& p : *i) {
                    if (!p.is_string())
                        return fail(err::BadLayout, "layout: `open` entry is not a string");
                    const std::string key = p.get<std::string>();
                    if (!panelFromKey(key))
                        return fail(err::UnknownPanel, "layout: unknown panel '" + key + "'");
                    into.open.push_back(key);
                }
        }
    }
    if (const auto ps = j.find("panels"); ps != j.end()) {
        if (!ps->is_object())
            return fail(err::BadLayout, "layout: `panels` is not an object");
        for (const auto& [key, value] : ps->items()) {
            if (!panelFromKey(key))
                return fail(err::UnknownPanel, "layout: unknown panel '" + key + "'");
            s.panels[key] = value;
        }
    }
    return s;
}

std::string LayoutState::serialize() const {
    registerLayoutSchema();
    return core::JsonEnvelope::serialize("ui.layout", toJson(), kSchema);
}

Result<LayoutState> LayoutState::parse(const std::string& text) {
    registerLayoutSchema();
    QXL_TRY_ASSIGN(const core::Envelope env, core::JsonEnvelope::parse(text, "ui.layout"));
    return fromJson(env.data);
}

} // namespace qlab::ui
