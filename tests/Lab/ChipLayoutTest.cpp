// Spec 17 §6.3 — chip placement is deterministic, non-overlapping and derived from the topology.
#include "Lab/ChipLayout.hpp"
#include "Core/Timer.hpp"
#include "Hardware/Hardware.hpp"
#include "Lab/Generators.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <map>

using namespace qlab;
using namespace qlab::lab;

namespace {
const hw::LoadedDevice& device(const char* id) {
    static std::map<std::string, hw::LoadedDevice> cache;
    auto it = cache.find(id);
    if (it == cache.end()) {
        auto d = hw::loadShippedDevice(id);
        REQUIRE(d.has_value());
        it = cache.emplace(id, std::move(*d)).first;
    }
    return it->second;
}

ChipLayout layout(const char* id) {
    const auto& dev = device(id);
    auto L = layoutChip(dev.device, &dev.calibration);
    if (!L)
        FAIL(L.error().format());
    return std::move(*L);
}
} // namespace

TEST_CASE("topology is classified from the coupling graph and layout coordinates") {
    CHECK(classifyTopology(device("sc_heavyhex_27").device) == ChipTopology::HeavyHex);
    CHECK(classifyTopology(device("sc_heavyhex_127").device) == ChipTopology::HeavyHex);
    CHECK(classifyTopology(device("sc_tunable_grid_54").device) == ChipTopology::Grid);
    // the 5-qubit T is neither a grid, a path nor a heavy-hex cell: spring embedder (spec 17 §6.3)
    CHECK(classifyTopology(device("sc_fixed_5").device) == ChipTopology::SpringEmbedded);
    // ion chains have no superconducting chip
    auto ion = layoutChip(device("ion_chain_11").device, nullptr);
    REQUIRE_FALSE(ion.has_value());
    CHECK(ion.error().code == kErrChip);
}

TEST_CASE("Xmon pads are sized from the charging energy") {
    // C_Sigma = e^2 / (2 E_C) with E_C/h = |alpha|; four arms of the coplanar strip capacitance
    constexpr double h = 6.62607015e-34, e = 1.602176634e-19, eps0 = 8.8541878128e-12;
    const double w = 24.0, g = 24.0, epsEff = 6.45;
    auto K = [](double k) {
        double a = 1.0, b = std::sqrt(1.0 - k * k);
        for (int i = 0; i < 40 && std::abs(a - b) > 1e-15; ++i) {
            double an = 0.5 * (a + b);
            b = std::sqrt(a * b);
            a = an;
        }
        return 3.14159265358979323846 / (2.0 * a);
    };
    double k = w / (w + 2.0 * g);
    double perLength = 4.0 * eps0 * epsEff * K(k) / K(std::sqrt(1.0 - k * k));
    for (double alpha : {-206e6, -330e6}) {
        double L = xmonArmLength_um(alpha, w, g, epsEff);
        double C = 4.0 * L * 1e-6 * perLength;
        CHECK(C == Catch::Approx(e * e / (2.0 * h * std::abs(alpha))).epsilon(1e-9));
        CHECK(L > 60.0);
        CHECK(L < 400.0);
    }
    // heavy-hex transmons (alpha ≈ −330 MHz) land near 100 µm, inside the descriptor's 100–300 µm
    CHECK(xmonArmLength_um(-330e6, w, g, epsEff) == Catch::Approx(100.4).margin(1.0));
}

TEST_CASE("heavy-hex positions put every coupled pair one pitch apart") {
    const auto& dev = device("sc_heavyhex_27").device;
    ChipLayout L = layout("sc_heavyhex_27");
    ChipLayoutOptions o;
    for (const auto& e : dev.edges) {
        double d = glm::length(L.qubits[e.a].pos_um - L.qubits[e.b].pos_um);
        INFO("edge " << e.a << "-" << e.b);
        CHECK(d == Catch::Approx(o.pitch_um).epsilon(1e-9));
    }
    // row-edge (bridge) qubits sit at the midpoint of their two neighbours
    std::map<std::uint32_t, std::vector<std::uint32_t>> adj;
    for (const auto& e : dev.edges) {
        adj[e.a].push_back(e.b);
        adj[e.b].push_back(e.a);
    }
    int bridges = 0;
    for (const auto& q : dev.qubits) {
        if (std::lround(q.pos[1]) % 2 == 0)
            continue;
        ++bridges;
        const auto& nb = adj[q.index];
        glm::dvec2 mid = 0.5 * (L.qubits[nb[0]].pos_um + L.qubits[nb[1]].pos_um);
        CHECK(glm::length(L.qubits[q.index].pos_um - mid) < 1e-9);
    }
    CHECK(bridges == 8);
}

TEST_CASE("grid positions follow the pitch and couplers sit at edge midpoints") {
    const auto& dev = device("sc_tunable_grid_54").device;
    ChipLayout L = layout("sc_tunable_grid_54");
    ChipLayoutOptions o;
    for (const auto& e : dev.edges) {
        REQUIRE(e.coupler.has_value());
        glm::dvec2 mid = 0.5 * (L.qubits[e.a].pos_um + L.qubits[e.b].pos_um);
        CHECK(glm::length(L.qubits[*e.coupler].pos_um - mid) < 1e-9);
        CHECK(glm::length(L.qubits[e.a].pos_um - L.qubits[e.b].pos_um) ==
              Catch::Approx(o.pitch_um).epsilon(1e-9));
        CHECK(L.qubits[*e.coupler].armLength_um < L.qubits[e.a].armLength_um); // coupler scale 0.6
    }
}

TEST_CASE("chip placement is deterministic and free of overlaps") {
    for (const char* id : {"sc_fixed_5", "sc_heavyhex_27", "sc_tunable_grid_54"}) {
        INFO(id);
        const auto& dev = device(id);
        core::Timer timer;
        auto a = layoutChip(dev.device, &dev.calibration);
        double ms = timer.ms();
        REQUIRE(a.has_value());
        auto b = layoutChip(dev.device, &dev.calibration);
        REQUIRE(b.has_value());
        CHECK(a->topology == b->topology);
        CHECK(a->size_um == b->size_um);
        REQUIRE(a->qubits.size() == b->qubits.size());
        for (std::size_t i = 0; i < a->qubits.size(); ++i)
            CHECK(a->qubits[i].pos_um == b->qubits[i].pos_um);
        REQUIRE(a->resonators.size() == b->resonators.size());
        for (std::size_t i = 0; i < a->resonators.size(); ++i) {
            CHECK(a->resonators[i].centerline_um == b->resonators[i].centerline_um);
            CHECK(a->resonators[i].angle_rad == b->resonators[i].angle_rad);
        }
        REQUIRE(a->driveLines.size() == b->driveLines.size());
        for (std::size_t i = 0; i < a->driveLines.size(); ++i)
            CHECK(a->driveLines[i].route.path_um == b->driveLines[i].route.path_um);
        REQUIRE(a->airbridges.size() == b->airbridges.size());
        for (std::size_t i = 0; i < a->airbridges.size(); ++i)
            CHECK(a->airbridges[i].pos_um == b->airbridges[i].pos_um);
        CHECK(a->overlaps().empty());
        for (const auto& d : a->diagnostics)
            WARN(d);
        WARN(std::string(id) + ": " + std::to_string(a->size_um.x / 1000.0) + " x " +
             std::to_string(a->size_um.y / 1000.0) + " mm, " +
             std::to_string(a->airbridges.size()) + " airbridges, " +
             std::to_string(a->bondPads.size()) + " pads, " + std::to_string(ms) + " ms");
    }
}

TEST_CASE("resonators, feedlines and control lines follow the device") {
    const auto& dev = device("sc_heavyhex_27");
    ChipLayout L = layout("sc_heavyhex_27");
    REQUIRE(L.qubits.size() == 27);
    REQUIRE(L.resonators.size() == 27);
    REQUIRE(L.feedlines.size() == dev.device.readout.feedlines.size());
    std::map<int, int> perFeedline;
    for (const auto& r : L.resonators) {
        CHECK(r.feedline >= 0);
        ++perFeedline[r.feedline];
        // λ/4 length from the device's resonator frequency
        double f = dev.device.readout.resonatorFrequencies[r.qubit].v;
        CHECK(r.frequency_Hz == Catch::Approx(f));
        CHECK(r.length_um == Catch::Approx(quarterWaveLength_m(f, 6.45) * 1e6));
        double len = 0.0;
        for (std::size_t i = 1; i < r.centerline_um.size(); ++i)
            len += glm::length(r.centerline_um[i] - r.centerline_um[i - 1]);
        CHECK(len == Catch::Approx(r.length_um).epsilon(1e-9));
        CHECK(r.tap_um == r.centerline_um.back());
    }
    for (const auto& [fl, n] : perFeedline) {
        INFO("feedline " << fl);
        CHECK(n <= 8); // spec 17 §6.3: at most 8 resonators per feedline
    }
    // the device's own feedline membership is honoured
    for (const auto& f : dev.device.readout.feedlines)
        for (auto q : f.qubits)
            CHECK(L.resonators[q].feedline == f.id);
    // one drive line per data qubit, no flux lines on a fixed-frequency device
    CHECK(L.driveLines.size() == 27);
    CHECK(L.fluxLines.empty());
    for (const auto& c : L.driveLines) {
        INFO("drive q" << c.target);
        CHECK(c.bondPad >= 0);
        CHECK(c.route.path_um.size() >= 2);
        CHECK(c.route.length_um() > 0.0);
    }
    // every feedline runs from an input pad through a Purcell filter to an output pad
    for (const auto& f : L.feedlines) {
        CHECK(f.padIn >= 0);
        CHECK(f.padOut >= 0);
        CHECK(f.route.path_um.size() > 4);
        CHECK(f.purcell.path_um.size() > 4);
        CHECK(f.purcell.length_um() > 1000.0);
    }
    CHECK(L.bondPads.size() >= L.signalNames.size());
    CHECK(L.airbridges.size() > 100);
    // tunable devices get a flux line per qubit and per coupler
    ChipLayout G = layout("sc_tunable_grid_54");
    CHECK(G.driveLines.size() == 54);
    CHECK(G.fluxLines.size() == 54 + 93);
}
