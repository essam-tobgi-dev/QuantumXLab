#include "Report/Import.hpp"

#include "Core/Paths.hpp"
#include "Hardware/Hardware.hpp"
#include <algorithm>
#include <cctype>

namespace qlab::report {
namespace {

lang::Diagnostic note(std::string message, std::uint32_t line) {
    lang::Diagnostic d;
    d.severity = lang::Severity::Info;
    d.error = Error(ErrorCode::Ok, std::move(message));
    SourceSpan span;
    span.line = line;
    span.column = 1;
    d.error.withSpan(std::move(span));
    return d;
}

std::string lowerExtension(const std::filesystem::path& p) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// The first non-blank, non-comment line of a program.
std::string_view firstStatement(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const std::size_t eol = text.find('\n', i);
        std::string_view line =
            text.substr(i, eol == std::string_view::npos ? std::string_view::npos : eol - i);
        while (!line.empty() &&
               (line.front() == ' ' || line.front() == '\t' || line.front() == '\r'))
            line.remove_prefix(1);
        if (!line.empty() && !line.starts_with("//"))
            return line;
        if (eol == std::string_view::npos)
            break;
        i = eol + 1;
    }
    return {};
}

std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t i = 0;
    while (i <= text.size()) {
        const std::size_t eol = text.find('\n', i);
        if (eol == std::string::npos) {
            lines.push_back(text.substr(i));
            break;
        }
        lines.push_back(text.substr(i, eol - i));
        i = eol + 1;
    }
    return lines;
}

bool replaceFirst(std::string& line, std::string_view from, std::string_view to) {
    const std::size_t at = line.find(from);
    if (at == std::string::npos)
        return false;
    line.replace(at, from.size(), to);
    return true;
}

} // namespace

std::string_view importKindName(ImportKind k) {
    switch (k) {
    case ImportKind::Qasm3:
        return "openqasm3";
    case ImportKind::Qasm2:
        return "openqasm2";
    case ImportKind::DeviceDirectory:
        return "device_directory";
    case ImportKind::Unknown:
        break;
    }
    return "unknown";
}

ImportKind classify(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec))
        return std::filesystem::exists(path / "device.json", ec) ? ImportKind::DeviceDirectory
                                                                 : ImportKind::Unknown;
    const std::string ext = lowerExtension(path);
    if (ext != ".qasm" && ext != ".qasm3" && ext != ".inc")
        return ImportKind::Unknown;
    auto text = core::readTextFile(path);
    if (!text)
        return ImportKind::Unknown;
    return firstStatement(*text).starts_with("OPENQASM 2") ? ImportKind::Qasm2 : ImportKind::Qasm3;
}

ImportedProgram convertQasm2(std::string text, const std::filesystem::path& origin) {
    ImportedProgram out;
    out.origin = origin;
    std::vector<std::string> lines = splitLines(text);
    for (std::size_t i = 0; i < lines.size(); ++i) {
        std::string& line = lines[i];
        const std::uint32_t lineNo = static_cast<std::uint32_t>(i + 1);
        if (line.find("OPENQASM 2.0") != std::string::npos &&
            replaceFirst(line, "OPENQASM 2.0", "OPENQASM 3.0")) {
            out.diagnostics.push_back(note("rewrote 'OPENQASM 2.0;' to 'OPENQASM 3.0;'", lineNo));
            out.converted = true;
            continue;
        }
        if (line.find("qelib1.inc") != std::string::npos &&
            replaceFirst(line, "qelib1.inc", "stdgates.inc")) {
            out.diagnostics.push_back(
                note("rewrote include \"qelib1.inc\" to \"stdgates.inc\"", lineNo));
            out.converted = true;
            continue;
        }
        // `opaque` has no OpenQASM 3 form the front end accepts; the declaration is kept as a
        // comment so the gate name is still visible to the reader (spec 23 §11 lists the rewrite).
        std::string trimmed = line;
        trimmed.erase(0, trimmed.find_first_not_of(" \t"));
        if (trimmed.starts_with("opaque ")) {
            line = "// [import] " + line;
            out.diagnostics.push_back(note(
                "commented out an 'opaque' declaration; define it with 'defcal' instead", lineNo));
            out.converted = true;
            continue;
        }
    }
    out.source.clear();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out.source += lines[i];
        if (i + 1 < lines.size())
            out.source += '\n';
    }
    // `qreg`/`creg`, `measure q -> c;` and the legacy gate aliases are accepted as they are
    // (spec 13 §4); they are noted so the user sees what the file still contains.
    if (out.source.find("qreg ") != std::string::npos ||
        out.source.find("creg ") != std::string::npos)
        out.diagnostics.push_back(
            note("'qreg'/'creg' declarations are read as 'qubit[n]'/'bit[n]'", 0));
    return out;
}

Result<ImportedProgram> importProgram(const std::filesystem::path& path) {
    const ImportKind kind = classify(path);
    if (kind == ImportKind::DeviceDirectory)
        return fail(ErrorCode::InvalidArgument,
                    path.string() + " is a device directory, not a program");
    if (kind == ImportKind::Unknown)
        return fail(ErrorCode::Unsupported,
                    "spec 23 §11: only .qasm, .qasm3 and .inc files are imported, not " +
                        path.string());
    QXL_TRY_ASSIGN(std::string text, core::readTextFile(path));
    if (kind == ImportKind::Qasm2)
        return convertQasm2(std::move(text), path);
    ImportedProgram out;
    out.source = std::move(text);
    out.origin = path;
    return out;
}

std::filesystem::path userDeviceDir() {
    return core::userDataDir() / "devices";
}

Result<std::filesystem::path> importDeviceDirectory(const std::filesystem::path& dir,
                                                    const std::filesystem::path& destRoot) {
    if (classify(dir) != ImportKind::DeviceDirectory)
        return fail(ErrorCode::InvalidArgument, dir.string() + " holds no device.json");
    // Spec 23 §11: the directory is linted before it is copied. `hw::loadDevice` is that lint: it
    // parses both documents and checks every cross-reference (spec 09 §1).
    QXL_TRY_ASSIGN(const hw::LoadedDevice loaded, hw::loadDevice(dir));

    const std::filesystem::path root = destRoot.empty() ? userDeviceDir() : destRoot;
    const std::string id = loaded.device.id.empty() ? dir.filename().string() : loaded.device.id;
    const std::filesystem::path dest = root / id;
    std::error_code ec;
    if (std::filesystem::exists(dest, ec))
        return fail(ErrorCode::InvalidArgument,
                    "a device '" + id + "' is already installed at " + dest.string());
    std::filesystem::create_directories(root, ec);
    if (ec)
        return fail(ErrorCode::Io, "cannot create " + root.string() + ": " + ec.message());
    std::filesystem::copy(dir, dest, std::filesystem::copy_options::recursive, ec);
    if (ec)
        return fail(ErrorCode::Io, "cannot copy device directory: " + ec.message());
    return dest;
}

} // namespace qlab::report
