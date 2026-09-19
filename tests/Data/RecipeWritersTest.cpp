#include "Core/Paths.hpp"
#include "Data/Data.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <filesystem>
using namespace qlab;
using namespace qlab::data;

TEST_CASE("formatDouble is shortest round-trip and locale independent") {
    REQUIRE(formatDouble(0.1) == "0.1");
    REQUIRE(formatDouble(1.0 / 3.0) == "0.3333333333333333");
    REQUIRE(formatDouble(4.812e9) == "4.812e+09");
    REQUIRE(formatDouble(-1.5e-7) == "-1.5e-07");
    REQUIRE(formatDouble(std::nan("")) == "nan");
}
TEST_CASE("CSV golden output with unit headers") {
    std::vector<double> t{0, 1e-6, 2e-6}, p{1.0, 0.5, 0.25};
    std::vector<CsvColumn> cols{{"t", "s", t}, {"p1", "", p}};
    REQUIRE(toCsv(cols) == "t (s),p1 ()\n0,1\n1e-06,0.5\n2e-06,0.25\n");
    Trace2D tr;
    tr.name = "s21";
    tr.xUnit = "Hz";
    tr.yUnit = "";
    tr.x = {1, 2};
    tr.y = {0.5, 0.6};
    tr.yIm = std::vector<double>{0.1, 0.2};
    REQUIRE(traceToCsv(tr) == "x (Hz),y_re (),y_im ()\n1,0.5,0.1\n2,0.6,0.2\n");
    auto j = traceToJson(tr);
    auto back = traceFromJson(j);
    REQUIRE(back);
    REQUIRE(back->yIm->at(1) == 0.2);
}
TEST_CASE("histogram JSON round trip") {
    Histogram h(2);
    h.add("01", 3);
    h.add("10", 5);
    auto j = histogramToJson(h);
    auto back = histogramFromJson(j);
    REQUIRE(back);
    REQUIRE(back->count("10") == 5);
    REQUIRE(back->total() == 8);
    REQUIRE(back->nbits() == 2);
}
TEST_CASE("recipe round trip in the shipped schema (spec 22 §6)") {
    Recipe r;
    r.id = "t1";
    r.title = "T1 relaxation";
    r.theory = "T04#3.2";
    r.program = "Assets/Programs/Calibration/t1.qasm";
    SweepAxis a;
    a.input = "t_delay";
    a.unit = "us";
    a.scale = "log";
    a.from = 0.1;
    a.to = 500.0;
    a.points = 41;
    a.values = {0.1, 500.0}; // filled by the reader; any non-empty list validates
    r.sweep = a;
    r.extract = {"t1.p1", "counts['1'] / shots", "binomial", 0, "", core::Json::object()};
    r.fitModel = "exp_decay";
    r.report = {"T1"};
    r.results = {{"T1", "fit.T1", "device.calibration.qubit[q].T1", 0.15}};
    REQUIRE(validateRecipe(r));

    const auto dir = std::filesystem::temp_directory_path() / "qxl_recipe_test";
    std::filesystem::create_directories(dir);
    REQUIRE(saveRecipe(dir / "t1.json", r));
    auto back = loadRecipe(dir / "t1.json");
    REQUIRE(back);
    REQUIRE(back->fitModel == "exp_decay");
    REQUIRE(back->report == std::vector<std::string>{"T1"});
    REQUIRE(back->sweep->input == "t_delay");
    REQUIRE(back->sweep->scale == "log");
    REQUIRE(back->sweep->values.size() == 41); // the log range expands on read
    REQUIRE(back->sweep->values.front() == Catch::Approx(0.1));
    REQUIRE(back->sweep->values[1] == Catch::Approx(0.1 * std::pow(5000.0, 1.0 / 40.0)));
    REQUIRE(back->extract.channel == "t1.p1");
    REQUIRE(back->extract.sigma == "binomial");
    REQUIRE(back->results.size() == 1);
    REQUIRE(back->results[0].tolerance == Catch::Approx(0.15));
    REQUIRE(loadRecipeDirectory(dir)->size() == 1);

    Recipe bad = r;
    bad.fitModel = "nope";
    REQUIRE_FALSE(validateRecipe(bad));
    Recipe both = r;
    both.generator = GeneratorSpec{"xeb", core::Json::object()};
    REQUIRE_FALSE(validateRecipe(both)); // a program and a generator are mutually exclusive
    Recipe neither = r;
    neither.program.clear();
    REQUIRE_FALSE(validateRecipe(neither));
    Recipe gen = neither;
    gen.generator = GeneratorSpec{"interleaved_rb", core::Json::object()};
    REQUIRE(validateRecipe(gen)); // a generator-only recipe is legal
    std::filesystem::remove_all(dir);
}

TEST_CASE("every shipped analysis recipe loads through data::Recipe") {
    // Regression: the reader expected `qlab.recipe` / `sweep.variable` / `fit_model` / a string
    // `extract`, none of which the shipped assets or spec 22 §6 use, so no recipe could load.
    const auto dir = core::assetDir() / "Analysis";
    auto all = loadRecipeDirectory(dir);
    if (!all)
        UNSCOPED_INFO(all.error().format());
    REQUIRE(all);
    REQUIRE(all->size() >= 18);
    int withProgram = 0, withGenerator = 0;
    for (const auto& r : *all) {
        INFO(r.id);
        REQUIRE_FALSE(r.id.empty());
        REQUIRE_FALSE(r.title.empty());
        REQUIRE(validateRecipe(r));
        REQUIRE_FALSE(r.extract.channel.empty());
        if (!r.program.empty())
            ++withProgram;
        if (r.generator)
            ++withGenerator;
        // A recipe that fits must name a model this build knows how to run.
        if (!r.fitModel.empty())
            REQUIRE(validateRecipe(r));
        // Round trip preserves what was read, unknown keys included.
        const core::Json j = recipeToJson(r);
        auto again = recipeFromJson(j);
        REQUIRE(again);
        REQUIRE(again->id == r.id);
        REQUIRE(again->fitModel == r.fitModel);
        REQUIRE(again->report == r.report);
        REQUIRE(again->results.size() == r.results.size());
        REQUIRE(again->sweep.has_value() == r.sweep.has_value());
        if (r.sweep)
            REQUIRE(again->sweep->values.size() == r.sweep->values.size());
    }
    REQUIRE(withProgram >= 15);
    REQUIRE(withGenerator == 2); // interleaved RB and XEB are runtime-generated families
}
