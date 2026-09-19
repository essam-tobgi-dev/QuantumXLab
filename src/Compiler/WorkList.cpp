// Spec 14 §5 — the optimizer's per-wire index over a node list.
#include "Compiler/Commutation.hpp"
#include "Compiler/OptimizeImpl.hpp"
#include <algorithm>

namespace qlab::compiler::detail {

WorkList::WorkList(std::vector<ir::Node> list, std::uint32_t qubitCount)
    : nodes(std::move(list)), qubits(qubitCount) {
    const std::size_t n = nodes.size();
    alive.assign(n, 1);
    after.resize(n);
    wireSeq.resize(qubits);
    dirty.assign(qubits, 1);
    touched.assign(qubits, 0);
    begin_.reserve(n + 1);
    wireFlat_.reserve(2 * n);
    position_.reserve(2 * n);
    action_.reserve(2 * n);
    auto place = [&](std::uint32_t i, std::uint32_t wire, std::uint8_t action) {
        if (wire >= qubits)
            return;
        wireFlat_.push_back(wire);
        position_.push_back(static_cast<std::uint32_t>(wireSeq[wire].size()));
        action_.push_back(action);
        wireSeq[wire].push_back(i);
    };
    for (std::uint32_t i = 0; i < n; ++i) {
        begin_.push_back(static_cast<std::uint32_t>(wireFlat_.size()));
        if (const auto* g = std::get_if<ir::Gate>(
                &nodes[i])) { // targets first, then controls: `Gate::wires()` order
            std::size_t k = 0;
            for (ir::Wire w : g->targets)
                place(i, w.index, wireAction(*g, k++));
            for (ir::Wire w : g->controls)
                place(i, w.index, wireAction(*g, k++));
            continue;
        }
        // A barrier or delay without operands spans every wire (spec 13 §3); control nodes occupy
        // the wires of their bodies, so nothing is moved or merged across them.
        for (ir::Wire w : ir::nodeWiresIn(nodes[i], qubits))
            place(i, w.index, 0);
    }
    begin_.push_back(static_cast<std::uint32_t>(wireFlat_.size()));
}

std::vector<ir::Node> WorkList::flush() {
    std::vector<ir::Node> out;
    out.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        if (alive[i])
            out.push_back(std::move(nodes[i]));
        for (ir::Node& n : after[i])
            out.push_back(std::move(n));
    }
    return out;
}

std::uint32_t WorkList::slotOf(std::uint32_t i, std::uint32_t wire) const {
    const auto ws = wiresOf(i);
    const auto it = std::find(ws.begin(), ws.end(), wire);
    return it == ws.end() ? npos : static_cast<std::uint32_t>(it - ws.begin());
}

std::uint32_t WorkList::previousOn(std::uint32_t i, std::uint32_t slot) const {
    if (slot >= begin_[i + 1] - begin_[i])
        return npos;
    const auto& seq = wireSeq[wireFlat_[begin_[i] + slot]];
    for (std::uint32_t p = position_[begin_[i] + slot]; p-- > 0;) {
        const std::uint32_t j = seq[p];
        if (!after[j].empty())
            return npos; // nodes spliced in after j are not indexed yet
        if (alive[j])
            return j;
    }
    return npos;
}

bool WorkList::commutes(std::uint32_t i, std::uint32_t j) const {
    for (std::uint32_t a = begin_[i]; a < begin_[i + 1]; ++a)
        for (std::uint32_t b = begin_[j]; b < begin_[j + 1]; ++b)
            if (wireFlat_[a] == wireFlat_[b] && (action_[a] & action_[b]) == 0)
                return false;
    return true;
}

bool WorkList::anyDirty(std::uint32_t i) const {
    for (std::uint32_t a = begin_[i]; a < begin_[i + 1]; ++a)
        if (dirty[wireFlat_[a]])
            return true;
    return false;
}

void WorkList::touch(std::uint32_t i) {
    for (std::uint32_t a = begin_[i]; a < begin_[i + 1]; ++a)
        touched[wireFlat_[a]] = 1;
}

bool isFusable(const ir::Gate& g) {
    return !g.opaque && g.controls.empty() && g.targets.size() == 1;
}

} // namespace qlab::compiler::detail
