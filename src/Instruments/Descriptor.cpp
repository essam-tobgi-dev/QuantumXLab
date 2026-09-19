#include "Instruments/Descriptor.hpp"
#include "Core/Paths.hpp"
#include <algorithm>
#include <format>

namespace qlab::instr {
namespace {
struct Term {
    std::string_view kind, field, term;
};
// Spec-sheet row → the setting (or "channel:<name>", or a term of another instrument) that enforces
// it.
constexpr Term kTerms[] = {
    {"sg_mw", "frequency", "frequency"},
    {"sg_mw", "output power", "power"},
    {"sg_mw", "phase noise at 10 kHz", "phase_noise_dbc"},
    {"sg_mw", "output on", "rf_on"},
    {"awg", "channels", "channels"},
    {"awg", "sample rate", "sample_rate"},
    {"awg", "resolution", "resolution_bits"},
    {"awg", "waveform memory", "memory_samples"},
    {"awg", "feedback latency", "controller:feedforward_latency_ns"},
    {"awg", "channel waveform", "channel:ch[0].waveform"},
    {"awg", "running", "channel:running"},
    {"awg", "current shot", "channel:seq.shot"},
    {"digitizer", "sample rate", "sample_rate"},
    {"digitizer", "resolution", "resolution_bits"},
    {"digitizer", "integration window", "window_ns"},
    {"digitizer", "IQ point", "channel:ch[0].iq"},
    {"digitizer", "trigger", "trigger"},
    {"dc_source", "channels", "channels"},
    {"dc_source", "range", "ch[0].current"},
    {"dc_source", "resolution", "ch[0].current"},
    {"dc_source", "current", "channel:ch[0].I"},
    {"dc_source", "voltage", "channel:ch[0].V"},
    {"flow_meter", "range", "range_max"},
    {"flow_meter", "flow", "channel:n3"},
    {"iq_mixer", "RF band", "rf_max"},
    {"iq_mixer", "LO leakage", "lo_leakage_dbc"},
    {"iq_mixer", "image rejection", "image_rejection_dbc"},
    {"oscilloscope", "bandwidth", "bandwidth"},
    {"oscilloscope", "sample rate", "sample_rate"},
    {"oscilloscope", "channels", "channels"},
    {"oscilloscope", "channel trace", "channel:ch[0]"},
    {"pressure_gauge", "range", "range_max"},
    {"pressure_gauge", "pressure", "channel:p"},
    {"power_meter", "frequency range", "freq_max"},
    {"power_meter", "power range", "range_max"},
    {"power_meter", "uncertainty", "uncertainty_db"},
    {"power_meter", "averaging time", "averaging_ms"},
    {"power_meter", "reading", "channel:p"},
    {"controller", "trigger rate", "shot_loop"},
    {"controller", "locked", "clock_ref"},
    // The rack's 10 MHz reference, clock distribution and trigger unit are all of class
    // `controller`.
    {"controller", "outputs", "outputs"},
    {"controller", "skew", "skew_ps"},
    {"controller", "stability", "stability"},
    {"controller", "jitter", "jitter_ps"},
    {"spectrum_analyzer", "frequency range", "center"},
    {"spectrum_analyzer", "resolution bandwidth", "rbw"},
    {"spectrum_analyzer", "displayed noise floor", "danl_dbm_hz"},
    {"spectrum_analyzer", "trace", "channel:trace"},
    {"thermometer_ruo2", "range", "range_max"},
    {"thermometer_ruo2", "excitation", "excitation"},
    {"thermometer_ruo2", "resistance", "channel:R"},
    {"thermometer_ruo2", "temperature", "channel:T"},
    {"thermometer_cernox", "range", "range_max"},
    {"thermometer_cernox", "resistance", "channel:R"},
    {"thermometer_cernox", "temperature", "channel:T"},
    {"vna", "frequency range", "f_stop"},
    {"vna", "dynamic range", "dynamic_range_db"},
    {"vna", "IF bandwidth", "if_bandwidth"},
    {"vna", "trace", "channel:s21"},
    {"vna", "span", "f_stop"},
    {"vna", "power", "power"},
};

std::string substituteIndices(std::string text) { // "ch[k].iq" / "gen[$i].f" → index 0
    for (std::string_view placeholder : {"[$i]", "[$k]", "[k]", "[i]"})
        for (auto at = text.find(placeholder); at != std::string::npos; at = text.find(placeholder))
            text.replace(at, placeholder.size(), "[0]");
    return text;
}
} // namespace

std::optional<std::string> specSheetTerm(std::string_view kind, std::string_view field) {
    for (auto const& t : kTerms)
        if (t.kind == kind && t.field == field)
            return std::string(t.term);
    return std::nullopt;
}

Result<std::optional<InstrumentDescriptor>> parseInstrumentDescriptor(const core::Json& data) {
    if (!data.is_object() || !data.contains("instrument"))
        return std::optional<InstrumentDescriptor>{};
    const core::Json& block = data["instrument"];
    InstrumentDescriptor d;
    d.componentId = data.value("id", std::string{});
    const std::string where = "component '" + d.componentId + "'";
    if (!block.is_object() || !block.contains("class") || !block["class"].is_string())
        return fail(err::Descriptor, where + ": 'instrument.class' missing");
    if (!block.contains("settings_schema") || !block["settings_schema"].is_string())
        return fail(err::Descriptor, where + ": 'instrument.settings_schema' missing");
    d.kind = block["class"].get<std::string>();
    d.settingsSchema = block["settings_schema"].get<std::string>();
    d.name = data.value("name", std::string{});
    d.modelName = data.value("model_name", std::string{});
    if (block.contains("channels") && block["channels"].is_array())
        for (auto const& c : block["channels"])
            if (c.is_string())
                d.channels.push_back(c.get<std::string>());
    if (data.contains("spec_sheet") && data["spec_sheet"].is_array())
        for (auto const& row : data["spec_sheet"]) {
            if (!row.is_object())
                continue;
            SpecSheetRow r;
            r.field = row.value("field", row.value("name", std::string{}));
            r.unit = row.value("unit", std::string{});
            r.binding = row.value("binding", std::string{});
            if (row.contains("typical") && row["typical"].is_array() && row["typical"].size() == 2)
                r.typical =
                    std::pair{row["typical"][0].get<double>(), row["typical"][1].get<double>()};
            d.specSheet.push_back(std::move(r));
        }
    return std::optional<InstrumentDescriptor>{std::move(d)};
}

Result<std::vector<InstrumentDescriptor>>
loadInstrumentDescriptors(const std::filesystem::path& componentsDir) {
    const std::filesystem::path dir =
        componentsDir.empty() ? core::assetDir() / "Lab" / "Components" : componentsDir;
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return fail(ErrorCode::Io, "no component directory " + dir.string());
    std::vector<InstrumentDescriptor> out;
    for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
        const std::filesystem::path file = entry.path() / "component.json";
        if (!entry.is_directory() || !std::filesystem::exists(file))
            continue;
        auto envelope = core::JsonEnvelope::load(file, "");
        if (!envelope)
            return fail(err::Descriptor, file.string() + ": " + envelope.error().message);
        QXL_TRY_ASSIGN(auto parsed, parseInstrumentDescriptor(envelope->data));
        if (parsed)
            out.push_back(std::move(*parsed));
    }
    std::sort(out.begin(), out.end(),
              [](const InstrumentDescriptor& a, const InstrumentDescriptor& b) {
                  return a.componentId < b.componentId;
              });
    return out;
}

std::vector<std::string> lintDescriptor(const InstrumentDescriptor& d,
                                        InstrumentRegistry& registry) {
    std::vector<std::string> problems;
    const std::string where = d.componentId + ": ";
    if (!registry.hasKind(d.kind)) {
        problems.push_back(where + "instrument.class '" + d.kind +
                           "' is not a registered instrument");
        return problems;
    }
    IInstrument* instrument = registry.find(d.kind, 0);
    if (!instrument) {
        auto created = registry.create(d.kind);
        if (!created) {
            problems.push_back(where + created.error().message);
            return problems;
        }
        instrument = *created;
    }
    if (instrument->settings().id != d.settingsSchema)
        problems.push_back(std::format("{}settings_schema '{}' is not the schema of {} ('{}')",
                                       where, d.settingsSchema, d.kind, instrument->settings().id));
    for (auto const& pattern : d.channels)
        if (!findChannel(*instrument, substituteIndices(pattern)))
            problems.push_back(
                std::format("{}channel '{}' is not a channel of {}", where, pattern, d.kind));
    for (auto const& row : d.specSheet) {
        const auto term = specSheetTerm(d.kind, row.field);
        if (!term) {
            problems.push_back(
                std::format("{}spec-sheet row '{}' has no setting or model term in {}", where,
                            row.field, d.kind));
            continue;
        }
        if (term->starts_with("channel:")) {
            if (!findChannel(*instrument, term->substr(8)))
                problems.push_back(
                    std::format("{}row '{}': no channel '{}'", where, row.field, term->substr(8)));
        } else if (const auto colon = term->find(':');
                   colon != std::string::npos) { // a term of another instrument
            const std::string kind = term->substr(0, colon);
            const auto sample = registry.find(kind, 0) ? registry.find(kind, 0)
                                                       : registry.create(kind).value_or(nullptr);
            if (!sample || !sample->settings().find(term->substr(colon + 1)))
                problems.push_back(
                    std::format("{}row '{}': no setting '{}'", where, row.field, *term));
        } else if (!instrument->settings().find(*term)) {
            problems.push_back(
                std::format("{}row '{}': no setting '{}' in {}", where, row.field, *term, d.kind));
        }
    }
    return problems;
}

} // namespace qlab::instr
