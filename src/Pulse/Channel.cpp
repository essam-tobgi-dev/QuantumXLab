#include "Pulse/Channel.hpp"
#include <charconv>
#include <format>

namespace qlab::pulse {
namespace {
constexpr auto kErr = ErrorCode::Pulse_;

Result<std::uint32_t> parseIndex(std::string_view s) {
    std::uint32_t v = 0;
    auto* first = s.data();
    auto* last = s.data() + s.size();
    auto [ptr, ec] = std::from_chars(first, last, v);
    if (ec != std::errc{} || ptr != last)
        return fail(kErr, std::format("'{}' is not a qubit index", s));
    return v;
}
} // namespace

std::string_view channelKindName(ChannelKind k) {
    switch (k) {
    case ChannelKind::Drive:
        return "drive";
    case ChannelKind::Control:
        return "control";
    case ChannelKind::Flux:
        return "flux";
    case ChannelKind::Measure:
        return "measure";
    case ChannelKind::Acquire:
        return "acquire";
    case ChannelKind::GlobalRaman:
        return "global_raman";
    case ChannelKind::Raman:
        return "raman";
    case ChannelKind::Bichromatic:
        return "bichromatic";
    case ChannelKind::Detect:
        return "detect";
    case ChannelKind::Pump:
        return "pump";
    }
    return "?";
}

int channelKindArity(ChannelKind k) {
    switch (k) {
    case ChannelKind::Control:
    case ChannelKind::Bichromatic:
        return 2;
    case ChannelKind::GlobalRaman:
    case ChannelKind::Detect:
    case ChannelKind::Pump:
        return 0;
    default:
        return 1;
    }
}

bool ChannelId::isDrivelike() const {
    return kind != ChannelKind::Acquire;
}

std::string ChannelId::toString() const {
    switch (kind) {
    case ChannelKind::Drive:
        return std::format("d[{}]", a);
    case ChannelKind::Control:
        return std::format("u[{},{}]", a, b);
    case ChannelKind::Flux:
        return std::format("f[{}]", a);
    case ChannelKind::Measure:
        return std::format("m[{}]", a);
    case ChannelKind::Acquire:
        return std::format("a[{}]", a);
    case ChannelKind::GlobalRaman:
        return "g";
    case ChannelKind::Raman:
        return std::format("r[{}]", a);
    case ChannelKind::Bichromatic:
        return std::format("ms[{},{}]", a, b);
    case ChannelKind::Detect:
        return "detect";
    case ChannelKind::Pump:
        return "pump";
    }
    return "?";
}

Result<ChannelId> parseChannel(std::string_view s) {
    if (s == "g")
        return ChannelId::globalRaman();
    if (s == "detect")
        return ChannelId::detect();
    if (s == "pump")
        return ChannelId::pump();

    auto open = s.find('[');
    if (open == std::string_view::npos || s.back() != ']')
        return fail(kErr, std::format("'{}' is not a channel name", s));
    std::string_view prefix = s.substr(0, open);
    std::string_view body = s.substr(open + 1, s.size() - open - 2);

    ChannelKind kind{};
    if (prefix == "d")
        kind = ChannelKind::Drive;
    else if (prefix == "u")
        kind = ChannelKind::Control;
    else if (prefix == "f")
        kind = ChannelKind::Flux;
    else if (prefix == "m")
        kind = ChannelKind::Measure;
    else if (prefix == "a")
        kind = ChannelKind::Acquire;
    else if (prefix == "r")
        kind = ChannelKind::Raman;
    else if (prefix == "ms")
        kind = ChannelKind::Bichromatic;
    else
        return fail(kErr, std::format("unknown channel prefix '{}'", prefix));

    auto comma = body.find(',');
    if (channelKindArity(kind) == 2) {
        if (comma == std::string_view::npos)
            return fail(kErr, std::format("channel '{}' needs two indices", s));
        QXL_TRY_ASSIGN(auto x, parseIndex(body.substr(0, comma)));
        QXL_TRY_ASSIGN(auto y, parseIndex(body.substr(comma + 1)));
        return ChannelId{kind, x, y};
    }
    if (comma != std::string_view::npos)
        return fail(kErr, std::format("channel '{}' takes one index", s));
    QXL_TRY_ASSIGN(auto x, parseIndex(body));
    return ChannelId{kind, x, 0};
}

} // namespace qlab::pulse
