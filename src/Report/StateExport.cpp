#include "Report/StateExport.hpp"
#include "Data/Fidelity.hpp"

#include <bit>
#include <cstring>
#include <format>
#include <fstream>

namespace qlab::report {
namespace {

using core::Json;

Status writeBinaryAtomic(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return fail(ErrorCode::Io, "cannot write " + tmp.string());
        if (!bytes.empty()) f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!f) return fail(ErrorCode::Io, "write failed: " + tmp.string());
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) return fail(ErrorCode::Io, "rename failed: " + ec.message());
    return {};
}

std::string shapeTuple(std::span<const std::size_t> shape) {
    if (shape.empty()) return "()";
    std::string s = "(";
    for (std::size_t i = 0; i < shape.size(); ++i) {
        if (i) s += ", ";
        s += std::to_string(shape[i]);
    }
    if (shape.size() == 1) s += ",";   // NumPy writes 1-D shapes as `(N,)`
    s += ")";
    return s;
}

Json qubitOrderJson(const StateContext& ctx) {
    Json j;
    j["convention"] = "little-endian, index = sum_k q_k d^k with d = " + std::to_string(ctx.levels);
    j["qubits"] = ctx.qubits;   // simulator index -> device qubit
    j["levels"] = ctx.levels;
    j["level_order"] = ctx.levelOrder.empty()
                           ? std::string("site k contributes level l_k with weight d^k, k = 0 least significant")
                           : ctx.levelOrder;
    return j;
}

} // namespace

std::string npyHeader(std::string_view descr, std::span<const std::size_t> shape, bool fortranOrder) {
    // NumPy format 1.0: 6-byte magic, 2 version bytes, uint16 little-endian header length, then the
    // dict padded with spaces to a 64-byte total and terminated by '\n'.
    std::string dict = std::format("{{'descr': '{}', 'fortran_order': {}, 'shape': {}, }}", descr,
                                   fortranOrder ? "True" : "False", shapeTuple(shape));
    const std::size_t prefix = 10;
    std::size_t total = prefix + dict.size() + 1;            // + '\n'
    const std::size_t pad = (64 - (total % 64)) % 64;
    dict.append(pad, ' ');
    dict.push_back('\n');
    total = prefix + dict.size();

    std::string out;
    out.reserve(total);
    out += '\x93';
    out += "NUMPY";
    out += static_cast<char>(1);   // major
    out += static_cast<char>(0);   // minor
    const std::uint16_t len = static_cast<std::uint16_t>(dict.size());
    out += static_cast<char>(len & 0xFF);
    out += static_cast<char>((len >> 8) & 0xFF);
    out += dict;
    return out;
}

Status writeNpy(const std::filesystem::path& path, std::string_view descr, std::span<const std::size_t> shape,
                std::span<const std::byte> data) {
    if constexpr (std::endian::native != std::endian::little)
        return fail(ErrorCode::Unsupported, "'<' dtypes need a little-endian host");
    const std::string header = npyHeader(descr, shape);
    std::vector<std::byte> bytes(header.size() + data.size());
    std::memcpy(bytes.data(), header.data(), header.size());
    if (!data.empty()) std::memcpy(bytes.data() + header.size(), data.data(), data.size());
    return writeBinaryAtomic(path, bytes);
}

Status writeNpyComplex(const std::filesystem::path& path, std::span<const num::Complex> values,
                       std::span<const std::size_t> shape) {
    std::size_t expect = 1;
    for (std::size_t d : shape) expect *= d;
    if (expect != values.size())
        return fail(ErrorCode::InvalidArgument,
                    std::format("shape {} holds {} elements, the state has {}", shapeTuple(shape), expect, values.size()));
    return writeNpy(path, "<c16", shape,
                    std::span<const std::byte>(reinterpret_cast<const std::byte*>(values.data()),
                                               values.size() * sizeof(num::Complex)));
}

core::Json stateSidecar(const qsim::Snapshot& s, const StateContext& ctx, std::string_view file,
                        std::string_view dtype, std::span<const std::size_t> shape) {
    Json j;
    j["simulator_only"] = true;
    j["notice"] = std::string(simulatorOnlyNotice());
    j["file"] = std::string(file);
    j["dtype"] = std::string(dtype);
    j["shape"] = std::vector<std::size_t>(shape.begin(), shape.end());
    j["order"] = "C";
    j["backend"] = std::string(backendName(s.kind));
    j["qubit_order"] = qubitOrderJson(ctx);
    j["n_qubits"] = s.nQubits;
    j["gate_index"] = s.gateIndex;
    j["sim_time_ps"] = s.simTimePs;
    j["class"] = std::string(data::fidelityName(s.cls));
    j["identity"] = ctx.identity.toJson();
    if (!ctx.note.empty()) j["note"] = ctx.note;
    return j;
}

static Status writeSidecar(const std::filesystem::path& base, const Json& data) {
    ensureKinds();
    if (!allFinite(data)) return fail(ErrorCode::InvalidArgument, "state sidecar holds a non-finite number");
    std::filesystem::path p = base;
    p.replace_extension(".json");
    return core::writeTextFileAtomic(p, core::JsonEnvelope::serialize(kStateKind, data, kStateSchema));
}

Status exportStateVector(const std::filesystem::path& base, const qsim::Snapshot& s, const StateContext& ctx) {
    if (!s.amplitudes)
        return fail(ErrorCode::Unsupported, "this backend holds no state vector (" +
                                                std::string(backendName(s.kind)) + ")");
    std::filesystem::path npy = base;
    npy.replace_extension(".npy");
    const std::size_t shape[1] = {s.amplitudes->size()};
    QXL_TRY(writeNpyComplex(npy, *s.amplitudes, shape));
    return writeSidecar(base, stateSidecar(s, ctx, npy.filename().generic_string(), "<c16", shape));
}

Status exportDensityMatrix(const std::filesystem::path& base, const qsim::Snapshot& s, const StateContext& ctx) {
    if (!s.densityMatrix)
        return fail(ErrorCode::Unsupported, "this backend holds no density matrix (" +
                                                std::string(backendName(s.kind)) + ")");
    const num::Matrix& rho = *s.densityMatrix;
    std::filesystem::path npy = base;
    npy.replace_extension(".npy");
    const std::size_t shape[2] = {rho.rows, rho.cols};
    QXL_TRY(writeNpyComplex(npy, std::span<const num::Complex>(rho.data.data(), rho.data.size()), shape));
    return writeSidecar(base, stateSidecar(s, ctx, npy.filename().generic_string(), "<c16", shape));
}

std::string tableauText(const qsim::TableauExport& t) {
    std::string out = std::format("# stabilizer tableau, n = {}\n", t.n);
    out += "# stabilizers\n";
    for (const auto& g : t.stabilizers) out += g + "\n";
    if (!t.destabilizers.empty()) {
        out += "# destabilizers\n";
        for (const auto& g : t.destabilizers) out += g + "\n";
    }
    return out;
}

Status exportTableau(const std::filesystem::path& base, const qsim::Snapshot& s, const StateContext& ctx) {
    if (!s.tableau)
        return fail(ErrorCode::Unsupported, "this backend holds no stabilizer tableau (" +
                                                std::string(backendName(s.kind)) + ")");
    std::filesystem::path txt = base;
    txt.replace_extension(".txt");
    QXL_TRY(core::writeTextFileAtomic(txt, tableauText(*s.tableau)));
    const std::size_t shape[1] = {s.tableau->stabilizers.size()};
    return writeSidecar(base, stateSidecar(s, ctx, txt.filename().generic_string(), "pauli_strings", shape));
}

} // namespace qlab::report
