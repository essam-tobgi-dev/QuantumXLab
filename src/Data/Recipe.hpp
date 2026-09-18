#pragma once
// Spec 22 §6 — analysis recipes: named pipelines (program + sweep + extraction + fit + report)
// stored as JSON (envelope kind `analysis.recipe`). Execution is orchestrated by Runtime/UI;
// this module owns the data model, validation, and load/save only.
//
// The schema is the one in spec 22 §6 and in the shipped `Assets/Analysis/*.json`:
//   { id, title, theory, program?, sweep{input, unit, values, input2?, unit2?, values2?},
//     shots, extract{channel, y, sigma, ...}, fit{model, report[]}, results[], generator? }
// A recipe either names a `program` whose `input` declarations cover every swept variable, or
// names no program and declares `generator.kind` because the sequence family is built by the
// runtime (interleaved randomized benchmarking and cross-entropy benchmarking need uniformly
// random circuits, which no source file can spell — see SPEC_DEVIATIONS).
#include "Core/Error.hpp"
#include "Core/Json.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace qlab::data {

// One swept axis. `values` is either an explicit list or {linear|log: [a, b, n]}.
struct SweepAxis {
    std::string input;            // `input` variable name in the program
    std::string unit;             // display unit of the variable ("" = dimensionless)
    std::vector<double> values;   // explicit values, or the expansion of linear/log
    std::string scale = "list";   // list | linear | log (how `values` was written)
    double from = 0, to = 0;      // the range, when scale is linear|log
    int points = 0;               // the point count, when scale is linear|log
    std::vector<std::string> labels; // optional display labels, parallel to `values`
    std::vector<double> expand() const { return values; }
};

// How the y value of each sweep point is taken from the run (spec 22 §6; the expression grammar
// is spec 13 §8, shared with `pragma qlab.assert`, and is kept opaque here).
struct ExtractSpec {
    std::string channel;   // "t1.p1", "instr.dig.ch[q].iq"
    std::string y;         // "counts['1'] / shots"
    std::string sigma;     // binomial | multinomial | bootstrap | none
    int sequences = 0;     // random sequences averaged per point (0 = not a sequence family)
    std::string averageOver; // e.g. "sequences"
    core::Json extra;      // any other keys, preserved
};

// A row of the results table: measured value against the device's calibration or theory.
struct ResultRow {
    std::string label;
    std::string measured;  // "fit.T1"
    std::string model;     // "device.calibration.qubit[q].T1"
    double tolerance = 0;  // relative
};

// Declares that the runtime generates the circuit family (no source program can express it).
struct GeneratorSpec {
    std::string kind;      // "interleaved_rb" | "xeb" | …
    core::Json params;     // qubits, lengths/depths, sequences, interleave, seeded_by, …
};

struct Recipe {
    std::string id;                       // "t1"
    std::string title;
    std::string theory;                   // theory anchor, e.g. "T04#3.2"
    std::string description;
    std::string program;                  // asset-relative path; empty when `generator` is set
    std::optional<SweepAxis> sweep;       // primary axis
    std::optional<SweepAxis> sweep2;      // second axis (2D heatmap)
    ExtractSpec extract;
    std::string fitModel;                 // fit.model
    std::vector<std::string> report;      // fit.report: parameter names to display
    std::vector<ResultRow> results;
    std::optional<GeneratorSpec> generator;
    int shots = 1024;
    core::Json extra;                     // unknown top-level keys, preserved on save
};

Result<Recipe> recipeFromJson(const core::Json& data);
core::Json recipeToJson(const Recipe& r);
Result<Recipe> loadRecipe(const std::filesystem::path& path);
Status saveRecipe(const std::filesystem::path& path, const Recipe& r);
Result<std::vector<Recipe>> loadRecipeDirectory(const std::filesystem::path& dir);
// ids non-empty, exactly one of program/generator, sweep axes consistent, fit model known.
Status validateRecipe(const Recipe& r);

} // namespace qlab::data
