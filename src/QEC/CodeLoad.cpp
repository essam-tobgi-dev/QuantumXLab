// Spec 16 §1 — `Assets/QEC/<id>.json` ("qec.code" envelope) ↔ StabilizerCode. Unknown fields are
// ignored; a missing or mistyped field is reported by its path (DEVELOPMENT.md JSON rule).
#include "Core/Paths.hpp"
#include "QEC/Code.hpp"
#include <format>

namespace qlab::qec {
namespace {
using core::Json;
constexpr std::string_view kKind = "qec.code";
constexpr std::string_view kConvention =
    "Pauli strings are written left-to-right from qubit 0 (position i is qubit i).";

std::unexpected<Error> missing(std::string_view path, std::string_view expected) {
    return fail(err::BadJson, std::format("qec.code: field '{}' is missing or is not {}", path, expected));
}

Result<std::uint32_t> readUint(const Json& obj, std::string_view key, std::string_view path) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_number_unsigned()) return missing(path, "a non-negative integer");
    return it->get<std::uint32_t>();
}

Result<std::vector<PauliString>> readPaulis(const Json& obj, std::string_view key, std::string_view path) {
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) return missing(path, "an array of Pauli strings");
    std::vector<PauliString> out;
    for (std::size_t j = 0; j < it->size(); ++j) {
        const Json& e = (*it)[j];
        if (!e.is_string()) return missing(std::format("{}[{}]", path, j), "a Pauli string");
        auto p = PauliString::parse(e.get<std::string>());
        if (!p) {
            p.error().notes.push_back(std::format("field {}[{}]", path, j));
            return std::unexpected(std::move(p.error()));
        }
        out.push_back(std::move(*p));
    }
    return out;
}

Result<Coord2> readCoord(const Json& j, std::string_view path) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) return missing(path, "an [x, y] pair");
    return Coord2{j[0].get<double>(), j[1].get<double>()};
}

CodeFamily familyFromName(std::string_view s) {
    for (CodeFamily f : {CodeFamily::Repetition, CodeFamily::Shor, CodeFamily::Steane, CodeFamily::FiveQubit,
                         CodeFamily::SurfaceRotated})
        if (familyName(f) == s) return f;
    return CodeFamily::Other;
}

Result<AncillaSpec> readAncilla(const Json& j, const StabilizerCode& code, std::size_t index) {
    const std::string path = std::format("data.layout.ancilla[{}]", index);
    if (!j.is_object()) return missing(path, "an object");
    AncillaSpec a;
    const auto type = j.find("type");
    if (type == j.end() || !type->is_string()) return missing(path + ".type", "one of \"X\", \"Z\", \"M\"");
    const std::string t = type->get<std::string>();
    if (t == "X") a.type = CheckType::X;
    else if (t == "Z") a.type = CheckType::Z;
    else if (t == "M") a.type = CheckType::Mixed;
    else return missing(path + ".type", "one of \"X\", \"Z\", \"M\"");
    const auto coord = j.find("coord");
    if (coord == j.end()) return missing(path + ".coord", "an [x, y] pair");
    QXL_TRY_ASSIGN(a.coord, readCoord(*coord, path + ".coord"));
    const auto order = j.find("order");
    if (order == j.end() || !order->is_array()) return missing(path + ".order", "an array of data-qubit indices");
    for (std::size_t i = 0; i < order->size(); ++i) {
        const Json& q = (*order)[i];
        if (!q.is_number_unsigned() || q.get<std::uint64_t>() >= code.n)
            return missing(std::format("{}.order[{}]", path, i), std::format("a data-qubit index below n = {}", code.n));
        a.order.push_back(q.get<std::uint32_t>());
    }
    // Mixed checks repeat their generator so that the letters travel with the ancilla.
    if (const auto pauli = j.find("pauli"); pauli != j.end()) {
        if (!pauli->is_string()) return missing(path + ".pauli", "a Pauli string");
        QXL_TRY_ASSIGN(const PauliString p, PauliString::parse(pauli->get<std::string>()));
        if (index < code.stabilizers.size() && !p.sameLetters(code.stabilizers[index]))
            return fail(err::BadLayout, std::format("qec.code: {}.pauli = '{}' differs from stabilizers[{}] = '{}'", path,
                                                    p.str(), index, code.stabilizers[index].str()));
    }
    return a;
}

// Layout used when the file has none: data on a line, ancillas above it, gates in support order.
void synthesizeLayout(StabilizerCode& c) {
    c.dataLayout.clear();
    c.ancillas.clear();
    for (std::uint32_t q = 0; q < c.n; ++q) c.dataLayout.push_back({double(q), 0.0});
    for (std::uint32_t j = 0; j < c.checkCount(); ++j)
        c.ancillas.push_back({c.checkType(j), {double(j) + 0.5, 1.0}, c.stabilizers[j].support()});
}

} // namespace

Result<StabilizerCode> codeFromJson(const Json& d) {
    if (!d.is_object()) return missing("data", "an object");
    StabilizerCode c;
    const auto id = d.find("id");
    if (id == d.end() || !id->is_string()) return missing("data.id", "a string");
    c.id = id->get<std::string>();
    QXL_TRY_ASSIGN(c.n, readUint(d, "n", "data.n"));
    QXL_TRY_ASSIGN(c.k, readUint(d, "k", "data.k"));
    QXL_TRY_ASSIGN(c.d, readUint(d, "d", "data.d"));
    QXL_TRY_ASSIGN(c.stabilizers, readPaulis(d, "stabilizers", "data.stabilizers"));
    QXL_TRY_ASSIGN(c.logicalX, readPaulis(d, "logical_x", "data.logical_x"));
    QXL_TRY_ASSIGN(c.logicalZ, readPaulis(d, "logical_z", "data.logical_z"));
    if (const auto f = d.find("family"); f != d.end() && f->is_string()) c.family = familyFromName(f->get<std::string>());
    if (const auto t = d.find("theory"); t != d.end() && t->is_array())
        for (const Json& e : *t)
            if (e.is_string()) c.theoryRefs.push_back(e.get<std::string>());
    if (const auto n = d.find("notes"); n != d.end() && n->is_string()) c.notes = n->get<std::string>();

    const auto layout = d.find("layout");
    if (layout == d.end()) {
        synthesizeLayout(c);
    } else {
        if (!layout->is_object()) return missing("data.layout", "an object");
        const auto data = layout->find("data");
        if (data == layout->end() || !data->is_array()) return missing("data.layout.data", "an array of [x, y] pairs");
        for (std::size_t q = 0; q < data->size(); ++q) {
            QXL_TRY_ASSIGN(const Coord2 xy, readCoord((*data)[q], std::format("data.layout.data[{}]", q)));
            c.dataLayout.push_back(xy);
        }
        const auto anc = layout->find("ancilla");
        if (anc == layout->end() || !anc->is_array()) return missing("data.layout.ancilla", "an array of ancilla objects");
        for (std::size_t j = 0; j < anc->size(); ++j) {
            QXL_TRY_ASSIGN(AncillaSpec a, readAncilla((*anc)[j], c, j));
            c.ancillas.push_back(std::move(a));
        }
    }
    if (const auto dec = d.find("decoder"); dec != d.end() && dec->is_string()) {
        c.defaultDecoder = dec->get<std::string>();
        if (c.defaultDecoder != "lookup" && c.defaultDecoder != "union_find" && c.defaultDecoder != "mwpm")
            return missing("data.decoder", "one of \"lookup\", \"union_find\", \"mwpm\"");
    } else {
        c.defaultDecoder = c.isMatchable() ? "union_find" : "lookup";   // spec 16 §5 default
    }
    return c;
}

Json codeToJson(const StabilizerCode& c) {
    auto strings = [](const std::vector<PauliString>& ps) {
        Json a = Json::array();
        for (const PauliString& p : ps) a.push_back(p.str(p.phase != 0));
        return a;
    };
    Json d = Json::object();
    d["id"] = c.id;
    d["n"] = c.n;
    d["k"] = c.k;
    d["d"] = c.d;
    d["family"] = std::string(familyName(c.family));
    d["stabilizers"] = strings(c.stabilizers);
    d["logical_x"] = strings(c.logicalX);
    d["logical_z"] = strings(c.logicalZ);
    Json data = Json::array(), anc = Json::array();
    for (const Coord2& xy : c.dataLayout) data.push_back(Json::array({xy.x, xy.y}));
    for (std::size_t j = 0; j < c.ancillas.size(); ++j) {
        const AncillaSpec& a = c.ancillas[j];
        Json e = {{"type", std::string(checkTypeName(a.type))}, {"coord", Json::array({a.coord.x, a.coord.y})}, {"order", a.order}};
        if (a.type == CheckType::Mixed && j < c.stabilizers.size()) e["pauli"] = c.stabilizers[j].str();
        anc.push_back(std::move(e));
    }
    d["layout"] = {{"data", std::move(data)}, {"ancilla", std::move(anc)}};
    d["decoder"] = c.defaultDecoder;
    d["theory"] = c.theoryRefs;
    d["notes"] = c.notes;
    d["convention"] = std::string(kConvention);
    return d;
}

Result<StabilizerCode> loadCode(const std::filesystem::path& path, const VerifyOptions& options) {
    QXL_TRY_ASSIGN(const core::Envelope env, core::JsonEnvelope::load(path, kKind));
    auto annotate = [&](Error e) {
        e.notes.push_back("file: " + path.string());
        return std::unexpected(std::move(e));
    };
    auto code = codeFromJson(env.data);
    if (!code) return annotate(std::move(code.error()));
    if (auto ok = verifyCode(*code, options); !ok) return annotate(std::move(ok.error()));
    return code;
}

Result<StabilizerCode> loadShippedCode(std::string_view id, const VerifyOptions& options) {
    return loadCode(core::assetDir() / "QEC" / (std::string(id) + ".json"), options);
}

const std::vector<std::string>& shippedCodeIds() {
    static const std::vector<std::string> ids = {"repetition_bitflip_3", "repetition_phaseflip_3", "shor_9", "steane_7",
                                                 "five_qubit", "surface_rot_3", "surface_rot_5", "surface_rot_7"};
    return ids;
}

} // namespace qlab::qec
