#include "Instruments/Signal.hpp"
#include <cmath>
#include <format>

namespace qlab::instr {

std::size_t Signal::requestedSamples(const SignalRequest& r, double sampleRateHz, std::size_t whole) {
    if (r.samples) return r.samples;
    if (r.durationS > 0.0 && sampleRateHz > 0.0) return static_cast<std::size_t>(std::ceil(r.durationS * sampleRateHz));
    return whole;
}

void scaleSignal(Signal& s, double gainDb) {
    if (gainDb == 0.0) return;
    const double a = std::pow(10.0, gainDb / 20.0); // amplitude ratio of a power gain
    for (auto& z : s.samples) z *= a;
    s.fullScaleV *= a;
    s.noisePsdWPerHz *= a * a;
}

double meanPowerWatts(const Signal& s) {
    if (s.samples.empty()) return 0.0;
    double acc = 0.0;
    for (auto const& z : s.samples) acc += std::norm(z);
    return acc / static_cast<double>(s.samples.size()) / (2.0 * kZ0);
}

void SignalGraph::addNode(std::string name, const ISignalSource* source, std::string port) {
    std::lock_guard lk(*mu_);
    Node& n = nodes_[std::move(name)];
    if (source) {
        n.source = source;
        n.port = std::move(port);
    }
}

void SignalGraph::addEdge(std::string from, std::string to, double gainDb, std::string id) {
    std::lock_guard lk(*mu_);
    nodes_.try_emplace(from);
    nodes_.try_emplace(to);
    if (id.empty()) id = from + "->" + to;
    for (auto& e : edges_)
        if (e.to == to) { // one input per node: a new cable replaces the old one
            e = RouteEdge{std::move(id), std::move(from), std::move(to), gainDb, true};
            return;
        }
    edges_.push_back(RouteEdge{std::move(id), std::move(from), std::move(to), gainDb, true});
}

bool SignalGraph::hasNode(std::string_view name) const {
    std::lock_guard lk(*mu_);
    return nodes_.find(name) != nodes_.end();
}

std::vector<std::string> SignalGraph::nodes() const {
    std::lock_guard lk(*mu_);
    std::vector<std::string> out;
    for (auto const& [name, n] : nodes_) {
        (void)n;
        out.push_back(name);
    }
    return out;
}

std::vector<RouteEdge> SignalGraph::edges() const {
    std::lock_guard lk(*mu_);
    return edges_;
}

Result<void> SignalGraph::setConnected(std::string_view edgeId, bool connected) {
    std::lock_guard lk(*mu_);
    for (auto& e : edges_)
        if (e.id == edgeId) {
            e.connected = connected;
            return {};
        }
    return fail(err::BadRouting, std::format("routing: no cable '{}'", edgeId));
}

std::optional<RouteEdge> SignalGraph::inputOf(std::string_view node) const {
    std::lock_guard lk(*mu_);
    for (auto const& e : edges_)
        if (e.to == node) return e;
    return std::nullopt;
}

Result<SignalGraph::Path> SignalGraph::resolve(std::string_view node) const {
    std::lock_guard lk(*mu_);
    Path path;
    std::string at(node);
    for (std::size_t hops = 0;; ++hops) {
        auto it = nodes_.find(at);
        if (it == nodes_.end()) return fail(err::BadRouting, std::format("routing: unknown node '{}'", at));
        if (it->second.source) {
            path.source = it->second.source;
            path.port = it->second.port;
            path.sourceNode = at;
            return path;
        }
        if (hops > nodes_.size()) return fail(err::BadRouting, std::format("routing: cycle through '{}'", at));
        const RouteEdge* in = nullptr;
        for (auto const& e : edges_)
            if (e.to == at) in = &e;
        if (!in) return fail(err::BadRouting, std::format("routing: node '{}' has no source upstream", at));
        path.gainDb += in->gainDb;
        path.connected = path.connected && in->connected;
        at = in->from;
    }
}

Result<Signal> SignalGraph::signalAt(std::string_view node, const SignalRequest& request) const {
    // The path is resolved under the lock, the source evaluated without it (a mixer asks the graph again).
    QXL_TRY_ASSIGN(Path path, resolve(node));
    auto sig = path.source->signal(path.port, request);
    if (!sig) return sig;
    sig->node = std::string(node);
    scaleSignal(*sig, path.gainDb);
    if (!path.connected) { // spec 12 §11: the affected instrument shows no signal
        for (auto& z : sig->samples) z = Complex{};
        sig->connected = false;
        sig->note = std::format("no signal: a cable between '{}' and '{}' is disconnected", path.sourceNode, node);
    }
    return sig;
}

Result<double> SignalGraph::sampleRateAt(std::string_view node) const {
    QXL_TRY_ASSIGN(Path path, resolve(node));
    return path.source->sampleRateHz(path.port);
}

core::Json SignalGraph::toJson() const {
    std::lock_guard lk(*mu_);
    core::Json nodes = core::Json::array();
    for (auto const& [name, n] : nodes_) {
        (void)n;
        nodes.push_back(name);
    }
    core::Json edges = core::Json::array();
    for (auto const& e : edges_)
        edges.push_back({{"id", e.id}, {"from", e.from}, {"to", e.to}, {"gain_db", e.gainDb}, {"connected", e.connected}});
    return core::Json{{"nodes", nodes}, {"edges", edges}};
}

Result<SignalGraph> SignalGraph::fromJson(const core::Json& j) {
    if (!j.is_object() || !j.contains("edges") || !j["edges"].is_array())
        return fail(err::BadRouting, "routing: 'edges' missing");
    SignalGraph g;
    if (j.contains("nodes") && j["nodes"].is_array())
        for (auto const& n : j["nodes"])
            if (n.is_string()) g.addNode(n.get<std::string>());
    std::size_t k = 0;
    for (auto const& e : j["edges"]) {
        const std::string path = std::format("edges[{}]", k++);
        if (!e.is_object() || !e.contains("from") || !e["from"].is_string())
            return fail(err::BadRouting, std::format("routing: '{}.from' missing", path));
        if (!e.contains("to") || !e["to"].is_string())
            return fail(err::BadRouting, std::format("routing: '{}.to' missing", path));
        // An absent *or empty* id means the default "from->to" that addEdge substitutes; reading it
        // back with value() alone would hand setConnected the empty string.
        std::string id = e.value("id", std::string{});
        if (id.empty()) id = e["from"].get<std::string>() + "->" + e["to"].get<std::string>();
        g.addEdge(e["from"].get<std::string>(), e["to"].get<std::string>(), e.value("gain_db", 0.0), id);
        if (!e.value("connected", true)) QXL_TRY(g.setConnected(id, false));
    }
    return g;
}

} // namespace qlab::instr
