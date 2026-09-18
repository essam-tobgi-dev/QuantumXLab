// Spec 23 §8 (images and the baked annotation strip) and §11 (import).
#include "ReportTestUtil.hpp"
#include "Data/Fidelity.hpp"

#include "Hardware/Hardware.hpp"
#include "Lang/Sema.hpp"
#include "Report/Font5x7.hpp"

using namespace qlab;
using namespace qlab::report;

TEST_CASE("images: a PNG round-trips pixel for pixel (spec 23 §8)") {
    rtest::Sandbox box("png");
    Image img(16, 9, {12, 34, 56, 255});
    img.fillRect(4, 2, 5, 5, {255, 128, 0, 255});
    img.set(0, 0, {1, 2, 3, 255});
    img.set(15, 8, {9, 8, 7, 255});
    REQUIRE(writePng(box / "shot.png", img).has_value());
    CHECK_FALSE(std::filesystem::exists(std::filesystem::path(box / "shot.png").string() + ".tmp"));

    auto back = readPng(box / "shot.png");
    REQUIRE(back.has_value());
    CHECK(back->width == 16);
    CHECK(back->height == 9);
    CHECK(back->rgba == img.rgba);
    CHECK(back->get(0, 0) == Rgba{1, 2, 3, 255});
    CHECK(back->get(15, 8) == Rgba{9, 8, 7, 255});
    CHECK(back->get(5, 3) == Rgba{255, 128, 0, 255});

    CHECK_FALSE(writePng(box / "empty.png", Image()).has_value());
    CHECK_FALSE(readPng(box / "missing.png").has_value());
}

TEST_CASE("images: the annotation strip is baked under the capture (spec 23 §8)") {
    Annotation a;
    a.title = "Bell pair";
    a.device = "sc_fixed_5";
    a.timestamp = "2026-09-18T12:00:00Z";
    a.badges.push_back({"Bloch", data::FidelityClass::Numerical, true});
    a.badges.push_back({"Histogram", data::FidelityClass::Statistical, false});
    a.scale = 2;

    const std::vector<std::string> lines = annotationLines(a);
    REQUIRE(lines.size() == 3u);
    CHECK(lines[0] == "Bell pair");
    CHECK(lines[1].find("sc_fixed_5") != std::string::npos);
    CHECK(lines[1].find("2026-09-18T12:00:00Z") != std::string::npos);
    CHECK(lines[2].find("Bloch: Numerical") != std::string::npos);
    CHECK(lines[2].find("(sim-only)") != std::string::npos);
    CHECK(lines[2].find("Histogram: Statistical") != std::string::npos);

    Image shot(320, 60, {80, 90, 100, 255});
    const Image out = withAnnotation(shot, a);
    CHECK(out.width == shot.width);
    CHECK(out.height == shot.height + annotationHeight(a));
    // The capture itself is untouched.
    for (int y = 0; y < shot.height; ++y)
        for (int x = 0; x < shot.width; ++x) REQUIRE(out.get(x, y) == shot.get(x, y));
    // The strip is drawn: an accent rule, background, and text pixels in the foreground colour.
    CHECK(out.get(0, shot.height) == a.accent);
    int foreground = 0, accent = 0;
    for (int y = shot.height + 2; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            if (out.get(x, y) == a.foreground) ++foreground;
            if (out.get(x, y) == a.accent) ++accent;
        }
    CHECK(foreground > 100);   // the device/time line and the badges
    CHECK(accent > 100);       // the title line
    CHECK(out.get(out.width - 1, out.height - 1) == a.background);

    rtest::Sandbox box("annotate");
    REQUIRE(writeViewportPng(box / "plain.png", shot).has_value());
    REQUIRE(writeViewportPng(box / "annotated.png", shot, &a).has_value());
    auto plain = readPng(box / "plain.png");
    auto annotated = readPng(box / "annotated.png");
    REQUIRE(plain.has_value());
    REQUIRE(annotated.has_value());
    CHECK(plain->height == shot.height);
    CHECK(annotated->height == out.height);
    CHECK(annotated->rgba == out.rgba);

    // The same strip can be baked into a PNG the renderer already wrote (GL stays out of Report).
    REQUIRE(annotatePngFile(box / "plain.png", a).has_value());
    auto reloaded = readPng(box / "plain.png");
    REQUIRE(reloaded.has_value());
    CHECK(reloaded->height == out.height);
}

TEST_CASE("images: the 5x7 font covers printable ASCII and folds lowercase to uppercase") {
    CHECK(font::glyph('A').size() == 5u);
    CHECK(font::glyph('a').data() == font::glyph('A').data());
    CHECK(font::glyph(' ')[0] == 0x00);
    CHECK(font::glyph('0')[0] == 0x3E);
    CHECK(font::glyph('\x01').data() == font::glyph('?').data());   // unknown -> '?'
    CHECK(font::textWidth("abc", 1) == 17);                         // 3 * 6 - 1
    CHECK(font::textWidth("abc", 2) == 34);
    CHECK(font::textWidth("", 3) == 0);
    // Every glyph fits in seven rows.
    for (char c = 0x20; c < 0x60; ++c)
        for (std::uint8_t column : font::glyph(c)) CHECK(column < 0x80);
}

TEST_CASE("import: only the three formats of spec 23 §11 are accepted") {
    rtest::Sandbox box("import");
    const std::string qasm3 = "OPENQASM 3.0;\ninclude \"stdgates.inc\";\nqubit[1] q;\nh q[0];\n";
    REQUIRE(core::writeTextFileAtomic(box / "p.qasm", qasm3).has_value());
    REQUIRE(core::writeTextFileAtomic(box / "lib.inc", "gate mygate q { x q; }\n").has_value());
    REQUIRE(core::writeTextFileAtomic(box / "notes.txt", qasm3).has_value());
    CHECK(classify(box / "p.qasm") == ImportKind::Qasm3);
    CHECK(classify(box / "lib.inc") == ImportKind::Qasm3);
    CHECK(classify(box / "notes.txt") == ImportKind::Unknown);
    CHECK(classify(box / "missing.qasm") == ImportKind::Unknown);
    CHECK(importKindName(ImportKind::Qasm2) == "openqasm2");

    auto ok = importProgram(box / "p.qasm");
    REQUIRE(ok.has_value());
    CHECK(ok->source == qasm3);
    CHECK_FALSE(ok->converted);
    CHECK(ok->diagnostics.empty());

    auto rejected = importProgram(box / "notes.txt");
    REQUIRE_FALSE(rejected.has_value());
    CHECK(rejected.error().code == ErrorCode::Unsupported);
    CHECK(rejected.error().message.find("23 §11") != std::string::npos);
}

TEST_CASE("import: OpenQASM 2 is converted and every rewrite is reported (spec 23 §11)") {
    rtest::Sandbox box("import_qasm2");
    const std::string legacy = "OPENQASM 2.0;\ninclude \"qelib1.inc\";\nqreg q[2];\ncreg c[2];\n"
                               "opaque custom(theta) a, b;\nh q[0];\nCX q[0],q[1];\nu1(0.5) q[1];\n"
                               "measure q -> c;\n";
    REQUIRE(core::writeTextFileAtomic(box / "legacy.qasm", legacy).has_value());
    CHECK(classify(box / "legacy.qasm") == ImportKind::Qasm2);

    auto imported = importProgram(box / "legacy.qasm");
    REQUIRE(imported.has_value());
    CHECK(imported->converted);
    CHECK(imported->source.starts_with("OPENQASM 3.0;\n"));
    CHECK(imported->source.find("stdgates.inc") != std::string::npos);
    CHECK(imported->source.find("qelib1.inc") == std::string::npos);
    CHECK(imported->source.find("// [import] opaque custom") != std::string::npos);
    CHECK(imported->source.find("measure q -> c;") != std::string::npos);   // accepted as it is

    // Every rewrite is listed, with the line it happened on and no catalogue id.
    REQUIRE(imported->diagnostics.size() >= 3u);
    bool sawVersion = false, sawInclude = false, sawOpaque = false;
    for (const auto& d : imported->diagnostics) {
        CHECK(d.severity == lang::Severity::Info);
        CHECK(d.id().empty());
        if (d.error.message.find("OPENQASM 3.0") != std::string::npos) {
            sawVersion = true;
            CHECK(d.error.span->line == 1u);
        }
        if (d.error.message.find("stdgates.inc") != std::string::npos) {
            sawInclude = true;
            CHECK(d.error.span->line == 2u);
        }
        if (d.error.message.find("opaque") != std::string::npos) {
            sawOpaque = true;
            CHECK(d.error.span->line == 5u);
        }
    }
    CHECK(sawVersion);
    CHECK(sawInclude);
    CHECK(sawOpaque);

    // The converted text is a valid OpenQASM 3 program.
    auto parsed = lang::parseProgram(imported->source, "legacy.qasm");
    INFO((parsed ? std::string() : parsed.error().format()));
    REQUIRE(parsed.has_value());
    for (const auto& d : parsed->diagnostics) {
        INFO(d.id() << ": " << d.error.message);
        CHECK_FALSE(d.isError());
    }
}

TEST_CASE("import: a device directory is linted before it is copied (spec 23 §11)") {
    rtest::Sandbox box("import_device");
    const std::filesystem::path source = hw::deviceRoot() / "sc_fixed_5";
    REQUIRE(std::filesystem::exists(source / "device.json"));
    CHECK(classify(source) == ImportKind::DeviceDirectory);
    CHECK(userDeviceDir().parent_path() == core::userDataDir());

    auto imported = importDeviceDirectory(source, box.path());
    INFO((imported ? std::string() : imported.error().format()));
    REQUIRE(imported.has_value());
    CHECK(imported->filename() == "sc_fixed_5");
    CHECK(std::filesystem::exists(*imported / "calibration.json"));
    CHECK(hw::loadDevice(*imported).has_value());

    // Importing it twice does not silently overwrite the installed copy.
    auto again = importDeviceDirectory(source, box.path());
    REQUIRE_FALSE(again.has_value());
    CHECK(again.error().message.find("already installed") != std::string::npos);

    // A directory that fails the lint is never copied.
    const std::filesystem::path broken = box / "broken";
    std::filesystem::create_directories(broken);
    REQUIRE(core::writeTextFileAtomic(broken / "device.json", "{\"qxl\": {\"kind\": \"device\"}, \"data\": {}}")
                .has_value());
    auto bad = importDeviceDirectory(broken, box / "dest");
    REQUIRE_FALSE(bad.has_value());
    CHECK_FALSE(std::filesystem::exists(box / "dest"));
}
