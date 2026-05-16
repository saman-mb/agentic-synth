#include "mapper/PromptSanitizer.h"

#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace agentic_synth::mapper {

namespace {

struct Mapping {
    std::string_view from;
    std::string_view to;
};

// Order matters: longer keys first to avoid partial-match clobbering
// (e.g. "death" before any future "dea" key).
constexpr std::array<Mapping, 8> kMap{{
    {"menacing", "intense"},
    {"violent",  "aggressive"},
    {"horror",   "uneasy"},
    {"scary",    "unsettling"},
    {"death",    "ending"},
    {"dread",    "tension"},
    {"evil",     "dark"},
    {"kill",     "drop"},
}};

inline char to_lower_ascii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

// Case-insensitive substring match: returns true iff `s` at `pos` starts
// with `needle` (compared lower-cased).
bool match_at(const std::string& s, std::size_t pos, std::string_view needle) noexcept {
    if (pos + needle.size() > s.size())
        return false;
    for (std::size_t i = 0; i < needle.size(); ++i) {
        if (to_lower_ascii(s[pos + i]) != needle[i])
            return false;
    }
    return true;
}

// Preserve the *first character's* case from the input. The rest of the
// replacement is emitted as-is (lower). Keeps "Horror" → "Uneasy" rather
// than "uneasy", and "HORROR" → "Uneasy" (good-enough for log readability).
std::string apply_case(std::string_view replacement, char first_input) {
    std::string out(replacement);
    if (!out.empty() && first_input >= 'A' && first_input <= 'Z')
        out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

bool is_word_boundary(const std::string& s, std::size_t pos) noexcept {
    // Word boundary if at edge or adjacent char is non-alphanumeric.
    if (pos == 0 || pos >= s.size())
        return true;
    const auto c = static_cast<unsigned char>(s[pos]);
    return !std::isalnum(c);
}

} // namespace

std::string sanitizePromptForSafety(const std::string& prompt) {
    if (prompt.empty())
        return prompt;

    std::string out;
    out.reserve(prompt.size());

    std::size_t i = 0;
    while (i < prompt.size()) {
        bool matched = false;
        // Only attempt a match at a word boundary so substrings like
        // "killer" don't accidentally rewrite to "dropper" — keeps proper
        // nouns and compound words intact.
        if (is_word_boundary(prompt, i == 0 ? 0 : i - 1) || i == 0 || !std::isalnum(static_cast<unsigned char>(prompt[i - 1]))) {
            for (const auto& m : kMap) {
                if (match_at(prompt, i, m.from)) {
                    // Trailing boundary check — full-word match only.
                    const std::size_t after = i + m.from.size();
                    const bool trailing_ok = after >= prompt.size() ||
                                             !std::isalnum(static_cast<unsigned char>(prompt[after]));
                    if (!trailing_ok)
                        continue;
                    out += apply_case(m.to, prompt[i]);
                    i += m.from.size();
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            out += prompt[i];
            ++i;
        }
    }
    return out;
}

} // namespace agentic_synth::mapper
