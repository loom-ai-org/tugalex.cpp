#include <algorithm>
#include <iterator>

#include "detail.hpp"

namespace tugalex::detail {

namespace {
#include "unicode_tables.inc"

template <size_t N>
bool in_ranges(const char32_t (&table)[N][2], char32_t c) {
    size_t lo = 0, hi = N;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (c < table[mid][0]) {
            hi = mid;
        } else if (c > table[mid][1]) {
            lo = mid + 1;
        } else {
            return true;
        }
    }
    return false;
}

bool is_space(char32_t c) { return in_ranges(kSpace, c); }

bool is_ascii(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

constexpr char32_t kCapitalSigma = 0x03A3;

// Python's handle_capital_sigma: Σ lowercases to final ς when it follows a
// cased letter (skipping case-ignorables) and no cased letter follows it.
char32_t lower_sigma(const U32& text, size_t i) {
    size_t j = i;
    char32_t c = 0;
    bool found = false;
    while (j > 0) {
        c = text[--j];
        if (!in_ranges(kCaseIgnorable, c)) {
            found = true;
            break;
        }
    }
    bool final_sigma = found && in_ranges(kCased, c);
    if (final_sigma && i + 1 < text.size()) {
        size_t k = i + 1;
        for (; k < text.size(); ++k) {
            c = text[k];
            if (!in_ranges(kCaseIgnorable, c)) break;
        }
        final_sigma = k == text.size() || !in_ranges(kCased, c);
    }
    return final_sigma ? 0x03C2 : 0x03C3;
}
}  // namespace

namespace {
// The UTF-8 sequence a lead byte starts: its length, the bounds of the second
// byte, and the payload bits of the lead byte. Length 0 marks a byte that
// cannot start a sequence (C0, C1, F5-FF and every continuation byte).
struct Lead {
    size_t len;
    unsigned char lo, hi;
    char32_t bits;
};

Lead lead_of(unsigned char b) {
    if (b < 0x80) return {1, 0, 0, b};
    if (b >= 0xC2 && b <= 0xDF) return {2, 0x80, 0xBF, char32_t(b & 0x1F)};
    if (b == 0xE0) return {3, 0xA0, 0xBF, 0};
    if (b == 0xED) return {3, 0x80, 0x9F, 0xD};
    if (b >= 0xE1 && b <= 0xEF) return {3, 0x80, 0xBF, char32_t(b & 0x0F)};
    if (b == 0xF0) return {4, 0x90, 0xBF, 0};
    if (b == 0xF4) return {4, 0x80, 0x8F, 4};
    if (b >= 0xF1 && b <= 0xF3) return {4, 0x80, 0xBF, char32_t(b & 0x07)};
    return {0, 0, 0, 0};
}

// The length of the valid sequence at text[i], or 0 with `taken` set to the
// bytes of its maximal invalid subpart.
size_t sequence_at(std::string_view text, size_t i, char32_t& cp, size_t& taken) {
    const Lead lead = lead_of(static_cast<unsigned char>(text[i]));
    cp = lead.bits;
    taken = 1;
    if (lead.len == 0) return 0;
    for (size_t k = 1; k < lead.len; ++k) {
        if (i + k >= text.size()) return 0;
        const auto c = static_cast<unsigned char>(text[i + k]);
        if (c < (k == 1 ? lead.lo : 0x80) || c > (k == 1 ? lead.hi : 0xBF)) return 0;
        cp = (cp << 6) | (c & 0x3F);
        taken = k + 1;
    }
    return lead.len;
}
}  // namespace

// Python's bytes.decode("utf-8", "replace"): one U+FFFD for each maximal
// invalid subpart. The byte that breaks a sequence is not consumed.
U32 from_utf8(std::string_view text) {
    U32 out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        char32_t cp;
        size_t taken;
        if (const size_t len = sequence_at(text, i, cp, taken)) {
            out.push_back(cp);
            i += len;
        } else {
            out.push_back(0xFFFD);
            i += taken;
        }
    }
    return out;
}

bool valid_utf8(std::string_view text) {
    for (size_t i = 0; i < text.size();) {
        char32_t cp;
        size_t taken;
        const size_t len = sequence_at(text, i, cp, taken);
        if (len == 0) return false;
        i += len;
    }
    return true;
}

std::string clean(std::string_view text) {
    return valid_utf8(text) ? std::string(text) : to_utf8(from_utf8(text));
}

std::string to_utf8(std::u32string_view text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t cp : text) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

std::string py_strip(std::string_view text) {
    // Fast path: the ends are ASCII and not whitespace, so nothing is stripped.
    auto plain = [](char c) {
        auto b = static_cast<unsigned char>(c);
        return b > 0x20 && b < 0x80;
    };
    if (text.empty() || (plain(text.front()) && plain(text.back()) && is_ascii(text))) {
        return std::string(text);
    }
    const U32 wide = from_utf8(text);
    size_t begin = 0, end = wide.size();
    while (begin < end && is_space(wide[begin])) ++begin;
    while (end > begin && is_space(wide[end - 1])) --end;
    return to_utf8(std::u32string_view(wide).substr(begin, end - begin));
}

std::string py_lower(std::string_view text) {
    if (is_ascii(text)) {
        std::string out(text);
        for (char& c : out) {
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        return out;
    }
    const U32 wide = from_utf8(text);
    U32 out;
    out.reserve(wide.size());
    for (size_t i = 0; i < wide.size(); ++i) {
        char32_t c = wide[i];
        if (c == kCapitalSigma) {
            out.push_back(lower_sigma(wide, i));
            continue;
        }
        const auto* end = std::end(kLower);
        const auto* it = std::lower_bound(std::begin(kLower), end, c,
                                          [](const LowerEntry& e, char32_t v) { return e.cp < v; });
        if (it != end && it->cp == c) {
            out.append(it->to, it->len);
        } else {
            out.push_back(c);
        }
    }
    return to_utf8(out);
}

std::vector<std::string> py_split(std::string_view text) {
    const U32 wide = from_utf8(text);
    std::vector<std::string> out;
    size_t i = 0;
    while (i < wide.size()) {
        while (i < wide.size() && is_space(wide[i])) ++i;
        size_t start = i;
        while (i < wide.size() && !is_space(wide[i])) ++i;
        if (i > start) out.push_back(to_utf8(std::u32string_view(wide).substr(start, i - start)));
    }
    return out;
}

std::vector<std::string> split_on(std::string_view text, char sep, int maxsplit) {
    std::vector<std::string> out;
    size_t start = 0;
    while (maxsplit < 0 || static_cast<int>(out.size()) < maxsplit) {
        const size_t at = text.find(sep, start);
        if (at == std::string_view::npos) break;
        out.emplace_back(text.substr(start, at - start));
        start = at + 1;
    }
    out.emplace_back(text.substr(start));
    return out;
}

std::string replace_all(std::string_view text, std::string_view from, std::string_view to) {
    std::string out;
    size_t start = 0;
    for (size_t at; (at = text.find(from, start)) != std::string_view::npos; start = at + from.size()) {
        out.append(text.substr(start, at - start));
        out.append(to);
    }
    out.append(text.substr(start));
    return out;
}

}  // namespace tugalex::detail
