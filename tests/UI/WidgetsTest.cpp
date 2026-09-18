// Spec 19 §5 — the widget rules that are pure logic: unit-suffixed display with SI-prefix
// auto-selection, typed input in any unit of the same dimension, the drag modifiers, the undo stack
// and the keyboard table.
#include "UI/Format.hpp"
#include "UI/Shortcuts.hpp"
#include "UI/UndoStack.hpp"
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace qlab;
using namespace qlab::ui;
using Catch::Approx;

TEST_CASE("Widgets: every numeric shows its unit with an auto-selected SI prefix (spec 19 §5.1)") {
    // The three examples the spec itself gives.
    CHECK(format::value(12.4e9, "Hz") == "12.4 GHz");
    CHECK(format::value(0.035, "K") == "35 mK");
    CHECK(format::value(320e-9, "s") == "320 ns");
    // …and the neighbouring decades.
    CHECK(format::value(4.812e9, "Hz") == "4.812 GHz");
    CHECK(format::value(1.0e6, "Hz") == "1 MHz");
    CHECK(format::value(2.5e-6, "s") == "2.5 µs");
    CHECK(format::value(1.5e-3, "s") == "1.5 ms");
    CHECK(format::value(300.0, "K") == "300 K");
    CHECK(format::value(30e-6, "W") == "30 µW");
    CHECK(format::prefixedUnit(12.4e9, "Hz") == "GHz");
    CHECK(format::prefixedUnit(0.035, "K") == "mK");

    // Log-ratio units are displayed verbatim; a dimensionless value has no suffix.
    CHECK(format::value(-20.0, "dBm") == "-20 dBm");
    CHECK(format::value(-3.5, "dB") == "-3.5 dB");
    CHECK(format::value(0.5, "") == "0.5");
    // A non-finite value reads "—" (spec 17 §5).
    CHECK(format::value(std::numeric_limits<double>::quiet_NaN(), "Hz") == "—");
    CHECK(format::number(std::numeric_limits<double>::infinity()) == "—");

    // Value ± 1σ share one prefixed unit.
    CHECK(format::withSigma(4.812e9, 3.0e6, "Hz") == "4.812 ± 0.003 GHz");
    CHECK(format::withSigma(0.5, 0.01, "") == "0.5 ± 0.01");
    CHECK(format::withSigma(1.0, 0.0, "Hz") == "1 Hz");   // no σ: the plain value

    CHECK(format::integer(1'048'576) == "1 048 576");
    CHECK(format::integer(512) == "512");
    CHECK(format::integer(1000) == "1 000");
    CHECK(format::duration(3.2e-3) == "3.2 ms");
    CHECK(format::duration(72.0) == "1 min 12 s");
    CHECK(format::duration(7500.0) == "2 h 05 min");
    CHECK(format::bytes(1.5 * 1024.0 * 1024.0 * 1024.0) == "1.5 GiB");
    CHECK(format::bytes(512.0) == "512 B");
    CHECK(format::percent(0.9973) == "99.73 %");
    CHECK(format::percent(0.5, 0) == "50 %");
}

TEST_CASE("Widgets: typing accepts any registered unit of the same dimension (spec 19 §5.1)") {
    CHECK(format::parse("5 us", "s").value() == Approx(5e-6));
    CHECK(format::parse("5000 ns", "s").value() == Approx(5e-6));
    CHECK(format::parse("0.000005 s", "s").value() == Approx(5e-6));
    CHECK(format::parse("5us", "s").value() == Approx(5e-6));
    CHECK(format::parse("4.5 GHz", "Hz").value() == Approx(4.5e9));
    CHECK(format::parse("35 mK", "K").value() == Approx(0.035));
    // A bare number takes the field's unit.
    CHECK(format::parse("320", "ns").value() == Approx(320e-9));
    // A dimensionless field takes a bare number.
    CHECK(format::parse("0.75", "").value() == Approx(0.75));

    // The wrong dimension is refused, and the message names both units.
    const auto wrong = format::parse("5 GHz", "s");
    REQUIRE_FALSE(wrong.has_value());
    CHECK(wrong.error().message.find("GHz") != std::string::npos);
    CHECK(wrong.error().message.find("wrong dimension") != std::string::npos);
    // So is nonsense.
    REQUIRE_FALSE(format::parse("banana", "s").has_value());
    REQUIRE_FALSE(format::parse("5 bananas", "s").has_value());
}

TEST_CASE("Widgets: drag modifiers are fine, plain and coarse (spec 19 §5.2)") {
    CHECK(format::dragScale({false, false}) == Approx(1.0));
    CHECK(format::dragScale({true, false}) == Approx(0.1));   // Shift = fine
    CHECK(format::dragScale({false, true}) == Approx(10.0));  // Alt = coarser
    CHECK(format::dragScale({true, true}) == Approx(1.0));

    constexpr double kStep = 0.5;
    CHECK(format::applyDrag(10.0, 4.0, kStep, {false, false}, -1e9, 1e9) == Approx(12.0));
    CHECK(format::applyDrag(10.0, 4.0, kStep, {true, false}, -1e9, 1e9) == Approx(10.2));
    CHECK(format::applyDrag(10.0, 4.0, kStep, {false, true}, -1e9, 1e9) == Approx(30.0));
    // The range clamps, whichever way round it is given.
    CHECK(format::applyDrag(10.0, 1000.0, kStep, {}, 0.0, 12.0) == Approx(12.0));
    CHECK(format::applyDrag(10.0, -1000.0, kStep, {}, 12.0, 0.0) == Approx(0.0));
    // A non-finite step leaves the value alone rather than poisoning it.
    CHECK(format::applyDrag(10.0, 1.0, std::numeric_limits<double>::infinity(), {}, -1e9, 1e9) == Approx(10.0));
}

TEST_CASE("Widgets: the undo stack keeps 200 entries and groups a gesture (spec 19 §5.3)") {
    UndoStack stack;
    CHECK(stack.capacity() == 200);
    double shots = 1024.0;

    stack.pushValue<double>("Shots", &shots, 1024.0, 2048.0);
    shots = 2048.0;
    CHECK(stack.canUndo());
    CHECK(stack.undoLabel() == "Shots");
    REQUIRE(stack.undo());
    CHECK(shots == Approx(1024.0));
    CHECK(stack.canRedo());
    REQUIRE(stack.redo());
    CHECK(shots == Approx(2048.0));

    // A drag gesture is ONE entry however many frames it spans.
    stack.clear();
    double value = 0.0;
    stack.beginGroup("Drag frequency");
    for (int i = 0; i < 10; ++i) {
        const double from = value, to = value + 1.0;
        stack.push(Command{"step", [&value, from] { value = from; }, [&value, to] { value = to; }, {}});
        value = to;
    }
    stack.endGroup();
    CHECK(stack.size() == 1);
    CHECK(value == Approx(10.0));
    REQUIRE(stack.undo());
    CHECK(value == Approx(0.0));      // the whole gesture, in reverse
    REQUIRE(stack.redo());
    CHECK(value == Approx(10.0));

    // A merge key folds a continued gesture into the entry it extends.
    stack.clear();
    double v = 0.0;
    for (int i = 0; i < 5; ++i) {
        const double from = v, to = v + 1.0;
        stack.push(Command{"Frequency", [&v, from] { v = from; }, [&v, to] { v = to; }, "frequency"});
        v = to;
    }
    CHECK(stack.size() == 1);
    REQUIRE(stack.undo());
    CHECK(v == Approx(0.0));

    // The capacity is honoured: the oldest entries fall off the back.
    stack.clear();
    int counter = 0;
    for (int i = 0; i < 260; ++i)
        stack.push(Command{"tick", [&counter] { --counter; }, [&counter] { ++counter; }, {}});
    CHECK(stack.size() == 200);
    // A new edit clears the redo branch.
    REQUIRE(stack.undo());
    CHECK(stack.redoSize() == 1);
    stack.push(Command{"other", [] {}, [] {}, {}});
    CHECK(stack.redoSize() == 0);
    // A command without both closures is ignored rather than crashing later.
    const std::size_t before = stack.size();
    stack.push(Command{"broken", nullptr, nullptr, {}});
    CHECK(stack.size() == before);
}

TEST_CASE("Widgets: the keyboard table matches spec 19 §5 and round-trips through JSON") {
    const Shortcuts keys = Shortcuts::defaults();
    CHECK(keys.text(Action::Run) == "F5");
    CHECK(keys.text(Action::Stop) == "Shift+F5");
    CHECK(keys.text(Action::Compile) == "F6");
    CHECK(keys.text(Action::StepGate) == "F10");
    CHECK(keys.text(Action::StepShot) == "F11");
    CHECK(keys.text(Action::WorkspaceLab) == "Ctrl+1");
    CHECK(keys.text(Action::WorkspaceProgram) == "Ctrl+2");
    CHECK(keys.text(Action::WorkspaceAnalysis) == "Ctrl+3");
    CHECK(keys.text(Action::FocusSelection) == "F");
    CHECK(keys.text(Action::ToggleXray) == "X");
    CHECK(keys.text(Action::ToggleExploded) == "E");
    CHECK(keys.text(Action::ToggleCutaway) == "C");
    CHECK(keys.text(Action::SearchComponent) == "Ctrl+P");
    CHECK(keys.text(Action::Autocomplete) == "Ctrl+Space");
    CHECK(keys.text(Action::GotoDefinition) == "F12");
    CHECK(keys.text(Action::SaveProject) == "Ctrl+S");
    CHECK(keys.text(Action::ExportResults) == "Ctrl+Shift+E");
    CHECK(keys.text(Action::TogglePhysicalLab) == "Ctrl+L");
    CHECK(keys.text(Action::Undo) == "Ctrl+Z");
    CHECK(keys.text(Action::Redo) == "Ctrl+Shift+Z");

    // Every chord is bound to exactly one action.
    for (const auto& [action, chord] : keys.all()) CHECK(keys.lookup(chord) == action);
    CHECK(keys.lookup(Chord::parse("F9")) == Action::None);

    // Chord parsing and printing are inverse; a malformed chord is rejected.
    for (const char* text : {"F5", "Ctrl+1", "Shift+F5", "Ctrl+Shift+E", "Ctrl+Space"}) {
        const Chord c = Chord::parse(text);
        CHECK(c.valid());
        CHECK(c.text() == text);
    }
    CHECK(Chord::parse("Cmd+S").ctrl);      // the macOS spelling maps onto the same modifier
    CHECK_FALSE(Chord::parse("Meta+S").valid());
    CHECK_FALSE(Chord::parse("").valid());

    // JSON round-trip, and a user remap.
    const auto reloaded = Shortcuts::fromJson(keys.toJson());
    REQUIRE(reloaded.has_value());
    for (const auto& [action, chord] : keys.all()) {
        REQUIRE(reloaded->chord(action) != nullptr);
        CHECK(*reloaded->chord(action) == chord);
    }
    core::Json remap = core::Json::object();
    remap["run"] = "Ctrl+Enter";
    remap["stop"] = "";
    const auto custom = Shortcuts::fromJson(remap);
    REQUIRE(custom.has_value());
    CHECK(custom->text(Action::Run) == "Ctrl+Enter");
    CHECK(custom->text(Action::Stop).empty());          // an empty string unbinds
    CHECK(custom->text(Action::Compile) == "F6");        // everything else keeps its default

    core::Json bad = core::Json::object();
    bad["not_an_action"] = "F7";
    REQUIRE_FALSE(Shortcuts::fromJson(bad).has_value());
}
