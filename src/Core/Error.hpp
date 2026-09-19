#pragma once
// Spec 04 §2 — Result/Error. Recoverable failures return Result<T>; exceptions never cross modules.
#include <cstdint>
#include <expected>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

namespace qlab {

// Each module owns a block of codes (spec 04 §2). Values are stable identifiers for logs/tests.
enum class ErrorCode : std::uint32_t {
    Ok = 0,
    Unknown = 1,
    InvalidArgument = 2,
    OutOfRange = 3,
    NotFound = 4,
    Unsupported = 5,
    Io = 6,
    Parse = 7,
    Internal = 8,
    Cancelled = 9,
    ResourceExhausted = 10,
    Lang_ = 0x100,
    Compiler_ = 0x200,
    QSim_ = 0x300,
    Noise_ = 0x400,
    Hardware_ = 0x500,
    Pulse_ = 0x600,
    Cryo_ = 0x700,
    Runtime_ = 0x800,
    Instr_ = 0x900,
    Lab_ = 0xA00,
    Gfx_ = 0xB00,
    Ui_ = 0xC00,
    Data_ = 0xD00,
    Qec_ = 0xE00,
};

constexpr ErrorCode operator+(ErrorCode base, std::uint32_t offset) {
    return static_cast<ErrorCode>(static_cast<std::uint32_t>(base) + offset);
}

// Source span inside a user program (spec 13/14 diagnostics). Line/column are 1-based.
struct SourceSpan {
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::uint32_t endLine = 0;
    std::uint32_t endColumn = 0;
    std::string file;
};

struct Error {
    ErrorCode code = ErrorCode::Unknown;
    std::string message;
    std::optional<SourceSpan> span;
    std::vector<std::string> notes;
    std::string diagnosticId; // e.g. "QL3007" (spec 13 §8); empty for non-language errors
    std::source_location where = std::source_location::current();

    Error() = default;
    Error(ErrorCode c, std::string msg, std::source_location loc = std::source_location::current())
        : code(c), message(std::move(msg)), where(loc) {}

    Error& withSpan(SourceSpan s) {
        span = std::move(s);
        return *this;
    }
    Error& withNote(std::string n) {
        notes.push_back(std::move(n));
        return *this;
    }
    Error& withId(std::string id) {
        diagnosticId = std::move(id);
        return *this;
    }
    std::string format() const;
};

template <class T> using Result = std::expected<T, Error>;
using Status = std::expected<void, Error>;

template <class... Args>
[[nodiscard]] std::unexpected<Error>
fail(ErrorCode c, std::string msg, std::source_location loc = std::source_location::current()) {
    return std::unexpected(Error(c, std::move(msg), loc));
}
[[nodiscard]] inline std::unexpected<Error> fail(Error e) {
    return std::unexpected(std::move(e));
}

// QXL_TRY(expr): propagate an error from a Result-returning expression.
#define QXL_TRY(expr)                                                                              \
    do {                                                                                           \
        if (auto&& _r = (expr); !_r)                                                               \
            return std::unexpected(std::move(_r.error()));                                         \
    } while (0)

#define QXL_CAT_(a, b) a##b
#define QXL_CAT(a, b) QXL_CAT_(a, b)
// QXL_TRY_ASSIGN(decl, expr): `QXL_TRY_ASSIGN(auto v, f(x));` binds v to the value or returns the
// error.
#define QXL_TRY_ASSIGN(decl, expr)                                                                 \
    auto&& QXL_CAT(_qxl_r_, __LINE__) = (expr);                                                    \
    if (!QXL_CAT(_qxl_r_, __LINE__))                                                               \
        return std::unexpected(std::move(QXL_CAT(_qxl_r_, __LINE__).error()));                     \
    decl = std::move(*QXL_CAT(_qxl_r_, __LINE__))

} // namespace qlab
