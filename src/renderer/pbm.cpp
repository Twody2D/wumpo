#include "renderer/pbm.hpp"

#include "core/config.hpp"

#include <cctype>
#include <charconv>
#include <vector>

namespace wumpo::renderer {
namespace {

/// PBM allows comments (# to end of line) and free whitespace between tokens.
/// This reads the next token, skipping both.
[[nodiscard]] bool nextToken(std::string_view text, std::size_t& position,
                             std::string_view& token) {
    while (position < text.size()) {
        const char character = text[position];
        if (character == '#') {
            while (position < text.size() && text[position] != '\n') {
                ++position;
            }
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            ++position;
            continue;
        }
        break;
    }
    if (position >= text.size()) {
        return false;
    }

    const std::size_t start = position;
    while (position < text.size() &&
           std::isspace(static_cast<unsigned char>(text[position])) == 0 && text[position] != '#') {
        ++position;
    }
    token = text.substr(start, position - start);
    return true;
}

[[nodiscard]] bool parseInt(std::string_view token, int& value) {
    const char* begin = token.data();
    const char* end = begin + token.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} && result.ptr == end;
}

} // namespace

std::string toPbm(const Framebuffer& frame) {
    std::string out;
    // Header plus one character per pixel plus one newline per row.
    out.reserve(64 + static_cast<std::size_t>(Framebuffer::kWidth + 1) *
                         static_cast<std::size_t>(Framebuffer::kHeight));

    out += "P1\n";
    out += "# wumpo framebuffer\n";
    out += std::to_string(Framebuffer::kWidth);
    out += ' ';
    out += std::to_string(Framebuffer::kHeight);
    out += '\n';

    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            out += frame.pixel(x, y) ? '1' : '0';
        }
        out += '\n';
    }
    return out;
}

bool fromPbm(std::string_view text, Framebuffer& frame, std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };

    std::size_t position = 0;
    std::string_view token;

    if (!nextToken(text, position, token)) {
        return fail("empty file");
    }
    if (token != "P1") {
        return fail("not a P1 (plain text) PBM: magic is '" + std::string(token) + "'");
    }

    int width = 0;
    int height = 0;
    if (!nextToken(text, position, token) || !parseInt(token, width)) {
        return fail("missing or malformed width");
    }
    if (!nextToken(text, position, token) || !parseInt(token, height)) {
        return fail("missing or malformed height");
    }
    if (width != Framebuffer::kWidth || height != Framebuffer::kHeight) {
        return fail("image is " + std::to_string(width) + "x" + std::to_string(height) +
                    ", expected " + std::to_string(Framebuffer::kWidth) + "x" +
                    std::to_string(Framebuffer::kHeight));
    }

    // Collect pixels first, so a malformed file leaves the caller's frame alone.
    std::vector<bool> pixels;
    pixels.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    while (position < text.size()) {
        const char character = text[position];
        if (character == '#') {
            while (position < text.size() && text[position] != '\n') {
                ++position;
            }
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            ++position;
            continue;
        }
        if (character != '0' && character != '1') {
            return fail(std::string("unexpected character '") + character + "' in pixel data");
        }
        pixels.push_back(character == '1');
        ++position;
    }

    const auto expected = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (pixels.size() != expected) {
        return fail("expected " + std::to_string(expected) + " pixels, found " +
                    std::to_string(pixels.size()));
    }

    frame.clear();
    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const auto index =
                static_cast<std::size_t>(y) * Framebuffer::kWidth + static_cast<std::size_t>(x);
            frame.setPixel(x, y, pixels[index]);
        }
    }
    return true;
}

std::string diffToText(const Framebuffer& expected, const Framebuffer& actual) {
    std::string out;
    out.reserve(static_cast<std::size_t>(Framebuffer::kWidth + 1) *
                static_cast<std::size_t>(Framebuffer::kHeight));

    for (int y = 0; y < Framebuffer::kHeight; ++y) {
        for (int x = 0; x < Framebuffer::kWidth; ++x) {
            const bool want = expected.pixel(x, y);
            const bool got = actual.pixel(x, y);
            if (want == got) {
                out += want ? '#' : '.';
            } else {
                out += got ? '+' : '-';
            }
        }
        out += '\n';
    }
    return out;
}

} // namespace wumpo::renderer
