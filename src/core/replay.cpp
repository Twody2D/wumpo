#include "core/replay.hpp"

#include <cctype>
#include <charconv>

namespace wumpo::core {
namespace {

/// The largest replay this parser will accept, in ticks: about five hours at
/// 60 Hz. A bound is not paranoia - it is the difference between rejecting a
/// malformed file and letting it decide how much memory to allocate.
constexpr std::size_t kMaxInputs = 1'000'000;

class Scanner {
public:
    explicit Scanner(std::string_view text) : text_(text) {}

    void skipWhitespace() {
        while (position_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char expected) {
        skipWhitespace();
        if (position_ >= text_.size() || text_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] bool peek(char& out) {
        skipWhitespace();
        if (position_ >= text_.size()) {
            return false;
        }
        out = text_[position_];
        return true;
    }

    /// Reads a double-quoted string. Rejects escapes rather than half-supporting
    /// them: nothing this format writes contains one.
    [[nodiscard]] bool readString(std::string_view& out) {
        if (!consume('"')) {
            return false;
        }
        const std::size_t start = position_;
        while (position_ < text_.size() && text_[position_] != '"') {
            if (text_[position_] == '\\') {
                return false;
            }
            ++position_;
        }
        if (position_ >= text_.size()) {
            return false; // unterminated
        }
        out = text_.substr(start, position_ - start);
        ++position_;
        return true;
    }

    template<typename T>
    [[nodiscard]] bool readNumber(T& out) {
        skipWhitespace();
        const std::size_t start = position_;
        while (position_ < text_.size() &&
               (std::isdigit(static_cast<unsigned char>(text_[position_])) != 0 ||
                text_[position_] == '-')) {
            ++position_;
        }
        if (position_ == start) {
            return false;
        }
        const char* begin = text_.data() + start;
        const char* end = text_.data() + position_;
        const auto result = std::from_chars(begin, end, out);
        return result.ec == std::errc{} && result.ptr == end;
    }

    [[nodiscard]] bool atEnd() {
        skipWhitespace();
        return position_ >= text_.size();
    }

private:
    std::string_view text_;
    std::size_t position_ = 0;
};

} // namespace

std::string maskToText(input::ButtonMask mask) {
    std::string out;
    for (const input::Button button : input::kAllButtonList) {
        if (!input::isSet(mask, button)) {
            continue;
        }
        if (!out.empty()) {
            out += '+';
        }
        out += input::name(button);
    }
    return out;
}

std::optional<input::ButtonMask> maskFromText(std::string_view text) {
    if (text.empty()) {
        return input::ButtonMask{0};
    }

    input::ButtonMask mask = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t separator = text.find('+', start);
        const std::string_view piece =
            text.substr(start, separator == std::string_view::npos ? std::string_view::npos
                                                                   : separator - start);
        const auto button = input::parseButton(piece);
        if (!button.has_value()) {
            return std::nullopt;
        }
        mask = static_cast<input::ButtonMask>(mask | input::maskOf(*button));

        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
        if (start >= text.size()) {
            return std::nullopt; // trailing '+'
        }
    }
    return mask;
}

std::string toJson(const Replay& replay) {
    std::string out;
    out.reserve(64 + replay.inputs.size() * 8);

    out += "{\n";
    out += "  \"version\": " + std::to_string(replay.version) + ",\n";
    out += "  \"seed\": " + std::to_string(replay.seed) + ",\n";
    out += "  \"inputs\": [";

    for (std::size_t i = 0; i < replay.inputs.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        // Wrap every so often: a one-line array of 10000 entries is unreadable
        // in a diff and unpleasant in an editor.
        if (i % 16 == 0) {
            out += "\n    ";
        }
        out += '"';
        out += maskToText(replay.inputs[i]);
        out += '"';
    }

    out += "\n  ]\n}\n";
    return out;
}

bool fromJson(std::string_view text, Replay& replay, std::string* error) {
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };

    Scanner scanner(text);
    if (!scanner.consume('{')) {
        return fail("expected an object");
    }

    Replay parsed;
    bool saw_version = false;
    bool saw_seed = false;
    bool saw_inputs = false;

    char next = '\0';
    if (!scanner.peek(next)) {
        return fail("unexpected end of file");
    }

    if (next != '}') {
        while (true) {
            std::string_view key;
            if (!scanner.readString(key)) {
                return fail("expected a field name");
            }
            if (!scanner.consume(':')) {
                return fail("expected ':' after field '" + std::string(key) + "'");
            }

            if (key == "version") {
                if (!scanner.readNumber(parsed.version)) {
                    return fail("version must be a number");
                }
                saw_version = true;
            } else if (key == "seed") {
                if (!scanner.readNumber(parsed.seed)) {
                    return fail("seed must be a number");
                }
                saw_seed = true;
            } else if (key == "inputs") {
                if (!scanner.consume('[')) {
                    return fail("inputs must be an array");
                }
                if (!scanner.peek(next)) {
                    return fail("unterminated inputs array");
                }
                if (next != ']') {
                    while (true) {
                        std::string_view entry;
                        if (!scanner.readString(entry)) {
                            return fail("inputs must contain strings");
                        }
                        const auto mask = maskFromText(entry);
                        if (!mask.has_value()) {
                            return fail("unknown button combination '" + std::string(entry) + "'");
                        }
                        if (parsed.inputs.size() >= kMaxInputs) {
                            return fail("replay is longer than " + std::to_string(kMaxInputs) +
                                        " ticks");
                        }
                        parsed.inputs.push_back(*mask);

                        if (scanner.consume(',')) {
                            continue;
                        }
                        break;
                    }
                }
                if (!scanner.consume(']')) {
                    return fail("unterminated inputs array");
                }
                saw_inputs = true;
            } else {
                return fail("unknown field '" + std::string(key) + "'");
            }

            if (scanner.consume(',')) {
                continue;
            }
            break;
        }
    }

    if (!scanner.consume('}')) {
        return fail("expected '}'");
    }
    if (!scanner.atEnd()) {
        return fail("trailing data after the object");
    }

    if (!saw_version) {
        return fail("missing 'version'");
    }
    if (!saw_seed) {
        return fail("missing 'seed'");
    }
    if (!saw_inputs) {
        return fail("missing 'inputs'");
    }
    // Refusing a newer format is the point of storing a version at all: silently
    // reading unknown semantics would produce a run that is not the recorded one.
    if (parsed.version != Replay::kFormatVersion) {
        return fail("replay format version " + std::to_string(parsed.version) +
                    ", this build reads version " + std::to_string(Replay::kFormatVersion));
    }

    replay = std::move(parsed);
    return true;
}

} // namespace wumpo::core
