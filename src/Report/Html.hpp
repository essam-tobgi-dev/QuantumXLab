#pragma once
// Spec 23 §10 — the pieces a self-contained HTML report is built from: escaping, base64 data URIs,
// the `{{placeholder}}` substitution of the template (no templating library), OpenQASM syntax
// highlighting through inline CSS classes, and the inline-SVG figures.
//
// Nothing here emits an external reference: images arrive as data URIs, figures as inline SVG.
#include "Data/Histogram.hpp"
#include "Data/Fidelity.hpp"
#include "Data/Series.hpp"
#include "Report/Types.hpp"
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qlab::report {

std::string htmlEscape(std::string_view text);
std::string base64(std::span<const std::uint8_t> bytes);
// "data:<mime>;base64,…" — empty when `bytes` is empty.
std::string dataUri(std::string_view mime, std::span<const std::uint8_t> bytes);

using Placeholders = std::map<std::string, std::string, std::less<>>;
// Replaces every `{{name}}` of `tmpl`. An unknown name expands to nothing and is appended to
// `missing`; a `{{` without a closing `}}` is copied through unchanged.
std::string substitute(std::string_view tmpl, const Placeholders& values, std::vector<std::string>* missing = nullptr);

// `<span class="k">` keywords, `"c"` comments, `"s"` strings, `"n"` numbers, `"g"` gate calls on
// physical qubits. The text is escaped first, so the result is safe inside `<pre>`.
std::string highlightQasm(std::string_view source);

// A `<table>` with a header row; every cell is escaped.
std::string tableHtml(std::span<const std::string> headers, std::span<const std::vector<std::string>> rows,
                      std::string_view cssClass = "t");
// `<span class="badge …">Statistical</span>`, the fidelity badge of spec 00 §5 / 21 §2.
std::string fidelityBadge(data::FidelityClass cls, std::string_view prefix = {});

// Inline SVG bar chart of the `topK` most frequent bitstrings (spec 23 §10 results histogram).
std::string histogramSvg(const data::Histogram& h, std::size_t topK = 16, int width = 720, int height = 260);
// Inline SVG line plot of x/y (with an optional 1σ band), used for recipe fits and sweeps.
std::string seriesSvg(std::span<const double> x, std::span<const double> y, std::span<const double> sigma,
                      std::string_view xLabel, std::string_view yLabel, int width = 620, int height = 240);

// Spec 23 §10: a report must not reference anything outside itself. Reports every external URL,
// `<script>` tag and `@import` found in `html`.
bool isSelfContained(std::string_view html, std::vector<std::string>* offenders = nullptr);

} // namespace qlab::report
