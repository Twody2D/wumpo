#pragma once

#include "renderer/framebuffer.hpp"

#include <string>
#include <string_view>

namespace wumpo::renderer {

/// Framebuffers as plain-text PBM (P1): one character per pixel, one line per
/// row.
///
/// Text rather than PNG, on purpose. A 64x32 frame is 2048 characters, so a
/// golden baseline reads as a picture in a diff on GitHub - a reviewer can see
/// what changed in the image without opening anything. It also means the golden
/// tests need no image library, and no dependency that would have to survive the
/// move to hardware.
///
/// Lives here rather than in the desktop backend because the tests that use it
/// must run in the SDL-free build.

/// Serializes to PBM text. Never fails.
[[nodiscard]] std::string toPbm(const Framebuffer& frame);

/// Parses PBM text produced by `toPbm`, plus the small variations a person
/// might introduce by hand: comments, whitespace between pixels, CRLF endings.
///
/// Returns false and leaves `frame` untouched if the text is not a P1 image of
/// exactly the device's dimensions. Wrong-sized baselines are a mistake worth
/// reporting, not something to pad or crop silently.
[[nodiscard]] bool fromPbm(std::string_view text, Framebuffer& frame, std::string* error);

/// Renders the differences between two frames as text, for a failing test:
/// '.' matching unlit, '#' matching lit, '+' lit only in `actual`, '-' lit only
/// in `expected`.
[[nodiscard]] std::string diffToText(const Framebuffer& expected, const Framebuffer& actual);

} // namespace wumpo::renderer
