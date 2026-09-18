// Spec 14 §5.5 — two-qubit block resynthesis. A block is a maximal run of gates on one qubit pair:
// it starts at a two-qubit gate, takes every following gate on either wire as long as the gate
// stays inside the pair, and ends at the first node that leaves it. Its 4×4 unitary is
// resynthesised with the minimal number of cx, lowered to the target, and kept when that uses
// fewer two-qubit gates than the block did.
#include "Compiler/CircuitUtil.hpp"
#include "Compiler/Decompose.hpp"
#include "Compiler/Kak.hpp"
#include "Numerics/Matrix.hpp"
#include "Numerics/Tensor.hpp"
#include <array>

namespace qlab::compiler {
namespace {
constexpr std::uint32_t kNone = 0xFFFFFFFFu;

struct Block {
    std::uint32_t q0 = 0, q1 = 0;
    std::vector<std::uint32_t> members;   // node indices in order
    std::uint32_t twoQubit = 0;
    bool open = true;
};

// Unitary of the block on (q0, q1), q0 least significant.
Result<num::Matrix> unitaryOf(const Block& b, const std::vector<ir::Node>& nodes) {
    num::Matrix u = num::Matrix::identity(4);
    for (std::uint32_t i : b.members) {
        const ir::Gate& g = std::get<ir::Gate>(nodes[i]);
        QXL_TRY_ASSIGN(const num::Matrix m, ir::matrixOf(g));
        std::vector<std::size_t> at;
        for (ir::Wire w : g.wires()) at.push_back(w.index == b.q0 ? 0u : 1u);
        u = num::matmul(num::embed(m.view(), at, 2), u);
    }
    return u;
}
} // namespace

Result<KakStats> resynthesizeTwoQubitBlocks(ir::Circuit& c, const Target& target, std::stop_token stop) {
    KakStats stats;
    std::vector<ir::Node> nodes = takeNodes(c);
    Status bodies;
    for (ir::Node& n : nodes) {
        bodies = forEachBody(n, [&](ir::Circuit& body) -> Status {
            QXL_TRY_ASSIGN(const KakStats inner, resynthesizeTwoQubitBlocks(body, target, stop));
            stats.blocks += inner.blocks;
            stats.replaced += inner.replaced;
            return {};
        });
        if (!bodies) break;
    }
    // Collect the blocks.
    std::vector<Block> blocks;
    std::vector<std::uint32_t> openOn(c.qubitCount(), kNone);
    auto close = [&](std::uint32_t wire) {
        if (wire < openOn.size() && openOn[wire] != kNone) {
            Block& b = blocks[openOn[wire]];
            b.open = false;
            openOn[b.q0] = openOn[b.q1] = kNone;
        }
    };
    for (std::uint32_t i = 0; bodies && i < nodes.size(); ++i) {
        const auto* g = std::get_if<ir::Gate>(&nodes[i]);
        const bool plain = g && !g->opaque && g->width() >= 1 && g->width() <= 2;
        if (!plain) {
            for (ir::Wire w : ir::nodeWiresIn(nodes[i], c.qubitCount())) close(w.index);
            continue;
        }
        const auto ws = g->wires();
        if (ws.size() == 1) {
            if (openOn[ws[0].index] != kNone) blocks[openOn[ws[0].index]].members.push_back(i);
            continue;
        }
        const std::uint32_t a = ws[0].index, b = ws[1].index;
        if (openOn[a] == kNone || openOn[a] != openOn[b]) {
            close(a);
            close(b);
            openOn[a] = openOn[b] = static_cast<std::uint32_t>(blocks.size());
            blocks.push_back(Block{a, b, {}, 0, true});
        }
        Block& blk = blocks[openOn[a]];
        blk.members.push_back(i);
        ++blk.twoQubit;
    }
    // Resynthesise. All members of a block precede every node that closed it, so the new gates can
    // stand at the block's first member.
    std::vector<std::vector<ir::Gate>> replacement(nodes.size());
    std::vector<std::uint8_t> dead(nodes.size(), 0);
    for (const Block& b : blocks) {
        if (!bodies || b.twoQubit < 2) continue;
        if (stop.stop_requested()) { bodies = fail(ErrorCode::Cancelled, "compile cancelled"); break; }
        ++stats.blocks;
        stats.twoQubitBefore += b.twoQubit;
        stats.twoQubitAfter += b.twoQubit;
        auto u = unitaryOf(b, nodes);
        if (!u) continue;
        const SourceSpan& span = std::get<ir::Gate>(nodes[b.members.front()]).span;
        auto abstract = synthesizeTwoQubit(u->view(), ir::Wire{b.q0}, ir::Wire{b.q1}, span);
        if (!abstract) continue;
        std::vector<ir::Gate> native;
        bool lowered = true;
        for (const ir::Gate& g : *abstract) lowered = lowered && decomposeGate(g, target, c.isPhysical(), native).has_value();
        std::uint32_t after = 0;
        for (const ir::Gate& g : native) after += g.width() == 2 ? 1u : 0u;
        if (!lowered || after >= b.twoQubit) continue;
        stats.twoQubitAfter -= b.twoQubit - after;
        ++stats.replaced;
        for (std::uint32_t i : b.members) dead[i] = 1;
        replacement[b.members.front()] = std::move(native);
    }
    std::vector<ir::Node> out;
    out.reserve(nodes.size());
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        for (ir::Gate& g : replacement[i]) out.emplace_back(std::move(g));
        if (!dead[i]) out.push_back(std::move(nodes[i]));
    }
    setNodes(c, std::move(out));
    if (!bodies) return std::unexpected(bodies.error());
    return stats;
}

} // namespace qlab::compiler
