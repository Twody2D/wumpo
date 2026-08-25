#pragma once

#include "renderer/framebuffer.hpp"
#include "renderer/pbm.hpp"

#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

/// Support for screenshot tests: compare a rendered frame against a committed
/// baseline, pixel for pixel.
///
/// The tolerance is zero. Rendering is fully deterministic - integer Bresenham,
/// a compiled-in font, no floating point - so any difference at all is a change
/// somebody made, not noise to be filtered.
///
/// On failure the actual frame is written next to the baseline as
/// `<name>.actual.pbm` (ignored by git) and an ASCII diff is printed, so a
/// failing test says what moved rather than only that something did.
namespace wumpo::testing {

[[nodiscard]] inline std::filesystem::path goldenDirectory() {
    return std::filesystem::path(WUMPO_GOLDEN_DIR);
}

[[nodiscard]] inline bool readFile(const std::filesystem::path& path, std::string& out) {
    // Binary, so a baseline written on Windows and one written on Linux are
    // byte-identical and the zero-tolerance comparison stays honest.
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = buffer.str();
    return true;
}

/// Compares `frame` with the baseline named `name` in tests/golden.
///
/// A missing baseline fails rather than being created silently: a test that
/// writes its own expectation on first run passes forever without anyone having
/// looked at the picture. The actual frame is written out so that accepting it
/// is a deliberate `git mv` away.
inline void checkGolden(const std::string& name, const renderer::Framebuffer& frame) {
    const std::filesystem::path baseline_path = goldenDirectory() / (name + ".pbm");
    const std::filesystem::path actual_path = goldenDirectory() / (name + ".actual.pbm");

    std::string baseline_text;
    if (!readFile(baseline_path, baseline_text)) {
        std::ofstream(actual_path, std::ios::binary) << renderer::toPbm(frame);
        FAIL("no golden baseline at " << baseline_path.string() << "\nrendered frame written to "
                                      << actual_path.string()
                                      << "\nreview it, then rename it to accept it as the baseline"
                                      << "\n\n"
                                      << renderer::toPbm(frame));
        return;
    }

    renderer::Framebuffer baseline;
    std::string error;
    if (!renderer::fromPbm(baseline_text, baseline, &error)) {
        FAIL("baseline " << baseline_path.string() << " is unreadable: " << error);
        return;
    }

    if (baseline == frame) {
        // Clean up a stale failure artefact from an earlier run.
        std::error_code code;
        std::filesystem::remove(actual_path, code);
        return;
    }

    std::ofstream(actual_path, std::ios::binary) << renderer::toPbm(frame);
    FAIL("frame does not match " << baseline_path.string() << "\nactual written to "
                                 << actual_path.string()
                                 << "\n\n'+' lit only in actual, '-' lit only in baseline:\n\n"
                                 << renderer::diffToText(baseline, frame));
}

} // namespace wumpo::testing
