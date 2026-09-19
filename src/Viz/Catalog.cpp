// Spec 21 §3 — the view catalog: every view of §3.1–§3.17, in the order the workspace presets of
// spec 19 §3 list them. The App owns the instances and shares one `SelectionModel` with them.
#include "Viz/Viz.hpp"
#include <array>

namespace qlab::viz {
namespace {

// One factory per catalog entry; the id is the view's own `id()` and is asserted below.
using Factory = std::unique_ptr<IStateView> (*)();
template <class V> std::unique_ptr<IStateView> make() {
    return std::make_unique<V>();
}

struct Entry {
    std::string_view id;
    Factory factory;
};

constexpr std::array<Entry, 17> kCatalog{{
    {"bloch", &make<BlochView>},               // §3.1
    {"amplitudes", &make<AmplitudeView>},      // §3.2
    {"phasedisk", &make<PhaseDiskView>},       // §3.3
    {"qsphere", &make<QSphereView>},           // §3.4
    {"city", &make<CityView>},                 // §3.5
    {"hinton", &make<HintonView>},             // §3.6
    {"histogram", &make<HistogramView>},       // §3.7
    {"entanglement", &make<EntanglementView>}, // §3.8
    {"schmidt", &make<SchmidtView>},           // §3.9
    {"reduced", &make<ReducedView>},           // §3.10
    {"pauli", &make<PauliView>},               // §3.11
    {"coupling", &make<CouplingView>},         // §3.12
    {"circuit", &make<CircuitView>},           // §3.13
    {"pulses", &make<PulseView>},              // §3.14
    {"populations", &make<PopulationsView>},   // §3.15
    {"trajectories", &make<TrajectoryView>},   // §3.16
    {"wigner", &make<WignerView>},             // §3.17
}};

constexpr std::array<std::string_view, kCatalog.size()> kIds = [] {
    std::array<std::string_view, kCatalog.size()> ids{};
    for (std::size_t k = 0; k < kCatalog.size(); ++k)
        ids[k] = kCatalog[k].id;
    return ids;
}();

} // namespace

std::vector<std::unique_ptr<IStateView>> makeAllViews() {
    std::vector<std::unique_ptr<IStateView>> out;
    out.reserve(kCatalog.size());
    for (const Entry& e : kCatalog)
        out.push_back(e.factory());
    return out;
}

std::unique_ptr<IStateView> makeView(std::string_view id) {
    for (const Entry& e : kCatalog)
        if (e.id == id)
            return e.factory();
    return nullptr;
}

std::span<const std::string_view> viewIds() {
    return kIds;
}

} // namespace qlab::viz
