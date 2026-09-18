// Spec 17 §11 — layout.json / routing.json loading.
#include "Lab/Layout.hpp"
#include "Core/Json.hpp"
#include "Core/Paths.hpp"
#include <array>
#include <format>

namespace qlab::lab {

namespace {
struct StageName {
    std::string_view layout;
    cryo::Stage stage;
    std::string_view descriptor;
};
constexpr std::array<StageName, cryo::kStageCount> kStageNames{{
    {"rt", cryo::Stage::RT, "top_plate_300K"},
    {"s50", cryo::Stage::PT1, "stage_50K"},
    {"s4", cryo::Stage::PT2, "stage_4K"},
    {"still", cryo::Stage::STILL, "stage_still"},
    {"cp", cryo::Stage::CP, "stage_cp"},
    {"mxc", cryo::Stage::MXC, "stage_mxc"},
}};

glm::dvec3 vec3(const core::Json& j, glm::dvec3 fallback = glm::dvec3(0.0)) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0].get<double>(), j[1].get<double>(), j[2].get<double>()};
}
} // namespace

bool stageFromLayoutName(std::string_view name, cryo::Stage& out) {
    for (const auto& s : kStageNames)
        if (s.layout == name) {
            out = s.stage;
            return true;
        }
    return cryo::stageFromName(name, out);
}
std::string_view stageLayoutName(cryo::Stage s) { return kStageNames[static_cast<std::size_t>(s)].layout; }
std::string_view stageDescriptorId(cryo::Stage s) { return kStageNames[static_cast<std::size_t>(s)].descriptor; }

std::filesystem::path resolveLayoutDir(const std::filesystem::path& p) {
    std::error_code ec;
    if (std::filesystem::exists(p / "layout.json", ec)) return p;
    return core::assetDir() / "Lab" / "Layouts" / p;
}

std::filesystem::path resolveAssetPath(std::string_view s) {
    std::filesystem::path p{s};
    if (p.is_absolute()) return p;
    auto it = p.begin();
    if (it != p.end() && it->string() == "Assets") {
        std::filesystem::path rest;
        for (++it; it != p.end(); ++it) rest /= *it;
        return core::assetDir() / rest;
    }
    return core::assetDir() / p;
}

const LineBundle* RoutingSpec::bundle(cryo::LineKind k) const {
    for (const auto& b : bundles)
        if (b.kind == k) return &b;
    return bundles.empty() ? nullptr : &bundles.front();
}

glm::dvec3 RoutingSpec::slot(std::string_view name) const {
    auto it = slots.find(name);
    return it == slots.end() ? glm::dvec3(0.0) : it->second;
}

Result<LayoutSpec> loadLayout(const std::filesystem::path& dirOrId) {
    LayoutSpec L;
    L.dir = resolveLayoutDir(dirOrId);
    QXL_TRY_ASSIGN(auto env, core::JsonEnvelope::load(L.dir / "layout.json", "lab.layout"));
    const core::Json& d = env.data;
    L.id = d.value("id", L.dir.filename().string());
    if (auto r = d.find("room"); r != d.end()) {
        L.roomWidth_m = r->value("width_m", 7.0);
        L.roomDepth_m = r->value("depth_m", 6.0);
        L.roomHeight_m = r->value("height_m", 3.4);
    }
    if (auto f = d.find("fridge"); f != d.end() && f->is_object()) {
        L.fridgeModel = f->value("model", std::string{});
        if (auto p = f->find("position_m"); p != f->end()) L.fridgePosition_m = vec3(*p);
        L.shields = f->value("shields", true);
        L.magShield = f->value("mag_shield", true);
        if (auto fr = f->find("frame"); fr != f->end() && fr->is_object()) {
            L.frameWidth_m = fr->value("width_m", 1.4);
            L.frameDepth_m = fr->value("depth_m", 1.4);
            L.frameHeight_m = fr->value("height_m", 2.6);
        }
        const core::Json* heights = f->contains("stage_height_m") ? &(*f)["stage_height_m"] : nullptr;
        const core::Json* radii = f->contains("stage_radius_m") ? &(*f)["stage_radius_m"] : nullptr;
        if (auto st = f->find("stages"); st != f->end() && st->is_array())
            for (const auto& s : *st) {
                StageSpec spec;
                spec.name = s.get<std::string>();
                if (!stageFromLayoutName(spec.name, spec.stage))
                    return fail(kErrLayout, std::format("layout '{}': unknown stage '{}'", L.id, spec.name));
                spec.height_m = heights && heights->contains(spec.name) ? (*heights)[spec.name].get<double>() : 2.0;
                spec.radius_m = radii && radii->contains(spec.name) ? (*radii)[spec.name].get<double>() : 0.2;
                L.stages.push_back(std::move(spec));
            }
    }
    L.device = d.value("device", std::string{});
    if (d.contains("wiring")) L.wiringFile = resolveAssetPath(d["wiring"].get<std::string>());
    L.routingFile = d.contains("routing") ? resolveAssetPath(d["routing"].get<std::string>()) : L.dir / "routing.json";
    if (auto racks = d.find("racks"); racks != d.end() && racks->is_array())
        for (const auto& r : *racks) {
            RackSpec rk;
            rk.id = r.value("id", std::string{});
            if (auto p = r.find("position_m"); p != r.end()) rk.position_m = vec3(*p);
            rk.heightU = r.value("height_u", 42);
            if (auto u = r.find("units"); u != r.end() && u->is_array())
                for (const auto& entry : *u) {
                    std::string s = entry.get<std::string>();
                    auto colon = s.find(':');
                    int count = colon == std::string::npos ? 1 : std::stoi(s.substr(colon + 1));
                    rk.units.emplace_back(s.substr(0, colon), count);
                }
            L.racks.push_back(std::move(rk));
        }
    if (auto g = d.find("ghs"); g != d.end() && g->is_object() && g->contains("position_m")) L.ghs = vec3((*g)["position_m"]);
    if (auto cm = d.find("compressor"); cm != d.end() && cm->is_object()) {
        if (cm->contains("position_m")) L.compressor = vec3((*cm)["position_m"]);
        L.compressorOutside = cm->value("outside", false);
    }
    if (auto ch = d.find("chamber"); ch != d.end() && ch->is_object()) {
        if (ch->contains("position_m")) L.chamberPosition_m = vec3((*ch)["position_m"]);
        L.chamberRadius_m = ch->value("radius_m", 0.12);
    }
    if (auto ls = d.find("lasers"); ls != d.end() && ls->is_array())
        for (const auto& l : *ls)
            L.lasers.push_back({l.value("id", std::string("laser_path")), l.value("wavelength_nm", 369.0), l.value("role", std::string{})});
    if (auto props = d.find("props"); props != d.end() && props->is_array())
        for (const auto& p : *props) {
            PropSpec ps;
            if (p.is_string()) {
                ps.id = p.get<std::string>();
            } else {
                ps.id = p.value("id", std::string{});
                if (p.contains("position_m")) ps.position_m = vec3(p["position_m"]);
                ps.count = p.value("count", 1);
                if (auto sz = p.find("size_m"); sz != p.end() && sz->is_array() && sz->size() >= 2)
                    ps.size_m = glm::dvec2{(*sz)[0].get<double>(), (*sz)[1].get<double>()};
            }
            if (!ps.id.empty()) L.props.push_back(std::move(ps));
        }
    if (auto bm = d.find("bookmarks"); bm != d.end() && bm->is_object())
        for (auto it = bm->begin(); it != bm->end(); ++it) {
            LayoutBookmark b;
            b.name = it.key();
            b.view.name = it.key();
            b.view.position = vec3(it.value().value("pos", core::Json::array()), glm::dvec3(5, 3, 6));
            b.view.target = vec3(it.value().value("target", core::Json::array()));
            b.view.fovDeg = it.value().value("fov_deg", 45.0);
            b.scaleIsland = it.value().value("scale_island", std::string{});
            L.bookmarks.push_back(std::move(b));
        }
    if (auto ly = d.find("layers"); ly != d.end() && ly->is_array())
        for (const auto& l : *ly) L.layers.push_back(l.get<std::string>());
    if (auto lights = d.find("lights"); lights != d.end() && lights->is_array())
        for (const auto& l : *lights) {
            LightSpec spec;
            if (auto pos = l.find("position_m"); pos != l.end()) spec.position_m = vec3(*pos);
            if (auto sz = l.find("size_m"); sz != l.end() && sz->is_array() && sz->size() >= 2)
                spec.size_m = {(*sz)[0].get<double>(), (*sz)[1].get<double>()};
            if (auto col = l.find("color"); col != l.end()) spec.color = glm::vec3(vec3(*col, glm::dvec3(1.0)));
            spec.lumens = l.value("lumens", 4000.0);
            if (spec.lumens <= 0.0) return fail(kErrLayout, std::format("layout '{}': light with non-positive lumens", L.id));
            L.lights.push_back(spec);
        }
    return L;
}

Result<RoutingSpec> loadRouting(const std::filesystem::path& file) {
    RoutingSpec R;
    QXL_TRY_ASSIGN(auto env, core::JsonEnvelope::load(file, "lab.routing"));
    const core::Json& d = env.data;
    if (auto so = d.find("stage_order"); so != d.end() && so->is_array())
        for (const auto& s : *so) R.stageOrder.push_back(s.get<std::string>());
    if (auto ft = d.find("feedthrough_ring"); ft != d.end() && ft->is_object()) {
        R.feedthroughRadius_m = ft->value("radius_m", 0.24);
        R.feedthroughCount = ft->value("count", 96);
        R.feedthroughStart_deg = ft->value("start_angle_deg", 0.0);
    }
    if (auto lb = d.find("line_bundles"); lb != d.end() && lb->is_array())
        for (const auto& b : *lb) {
            LineBundle bundle;
            std::string kind = b.value("kind", std::string("xy"));
            if (!cryo::lineKindFromName(kind, bundle.kind))
                return fail(kErrLayout, std::format("routing: unknown line kind '{}'", kind));
            if (auto sec = b.find("sector_deg"); sec != b.end() && sec->is_array() && sec->size() >= 2) {
                bundle.sectorStart_deg = (*sec)[0].get<double>();
                bundle.sectorEnd_deg = (*sec)[1].get<double>();
            }
            if (auto rad = b.find("radius_m"); rad != b.end() && rad->is_object())
                for (auto it = rad->begin(); it != rad->end(); ++it) bundle.radius_m[it.key()] = it.value().get<double>();
            R.bundles.push_back(std::move(bundle));
        }
    if (auto sl = d.find("element_slots"); sl != d.end() && sl->is_object())
        for (auto it = sl->begin(); it != sl->end(); ++it)
            if (it.value().contains("stage_offset_m")) R.slots[it.key()] = vec3(it.value()["stage_offset_m"]);
    if (auto seg = d.find("segment_lengths_m"); seg != d.end() && seg->is_object())
        for (auto it = seg->begin(); it != seg->end(); ++it) R.segmentLengths_m[it.key()] = it.value().get<double>();
    R.rackToFridgeCable_m = d.value("rack_to_fridge_cable_m", 6.0);
    return R;
}

} // namespace qlab::lab
