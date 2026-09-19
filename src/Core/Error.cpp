#include "Core/Error.hpp"
#include <format>
namespace qlab {
std::string Error::format() const {
    std::string s;
    if (span) {
        s += std::format("{}:{}:{}: ", span->file.empty() ? "<program>" : span->file, span->line,
                         span->column);
    }
    if (!diagnosticId.empty())
        s += std::format("[{}] ", diagnosticId);
    s += message;
    for (auto& n : notes)
        s += "\n  note: " + n;
    return s;
}
} // namespace qlab
