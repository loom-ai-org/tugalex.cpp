// Module-level gender lookups: suffix_noun_gender, the noun-gender exceptions
// and the given names (Python's lru_cache'd loaders become function statics).
#include "detail.hpp"
#include "tugalex/tugalex.hpp"

namespace tugalex {

namespace {

// NOUN_GENDER_SUFFIXES as alternatives anchored by `$`. `not_after` is the
// negative lookbehind before the first character of the match.
struct Rule {
    const char* gender;
    std::vector<std::string_view> endings;
    std::string_view not_after;
};

const std::vector<Rule>& rules() {
    static const std::vector<Rule> table = {
        {"f", {"ção", "ções"}, {}},
        {"f", {"são", "sões"}, {}},
        {"f", {"dade", "dades"}, {}},
        {"f", {"tude", "tudes"}, {}},
        {"f", {"gem", "gens"}, {}},
        {"f", {"eza", "ezas"}, {}},
        {"f", {"ise", "ises"}, {}},
        {"m", {"o", "os"}, "ã"},
        {"m", {"or", "ores"}, {}},
        {"m", {"ume", "umes"}, {}},
        {"f", {"a", "as"}, {}},
    };
    return table;
}

bool ends_with(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// re.search(rule, word): Python's `$` matches at the end and before a final "\n".
bool matches(const Rule& rule, std::string_view word) {
    std::vector<std::string_view> ends = {word};
    if (!word.empty() && word.back() == '\n') ends.push_back(word.substr(0, word.size() - 1));
    for (auto text : ends) {
        for (auto ending : rule.endings) {
            if (!ends_with(text, ending)) continue;
            const auto before = text.substr(0, text.size() - ending.size());
            if (!rule.not_after.empty() && ends_with(before, rule.not_after)) continue;
            return true;
        }
    }
    return false;
}

using GenderTable = OrderedMap<std::optional<std::string>>;

// {row[key]: row[value] for row in csv.DictReader(f)}, with f opened as gzip
// text with newline="".
GenderTable load_dict_reader(const std::string& path, std::string_view key, std::string_view value) {
    const std::string text = detail::read_text(path, true);
    const auto rows = detail::csv_rows(detail::lines_of(text, true));
    GenderTable table;
    if (rows.empty()) return table;
    const auto& header = rows.front();
    // dict(zip(fieldnames, row)): a repeated field name keeps its last column.
    auto column = [&](std::string_view name) {
        for (size_t i = header.size(); i-- > 0;) {
            if (header[i] == name) return i;
        }
        throw std::out_of_range(std::string(name));
    };
    const size_t key_at = column(key), value_at = column(value);
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (row.empty()) continue;
        if (key_at >= row.size()) throw std::out_of_range("a row has no " + std::string(key) + " field");
        // A short row maps the missing fields to None (DictReader's restval).
        std::optional<std::string> field;
        if (value_at < row.size()) field = row[value_at];
        table.assign(row[key_at], std::move(field));
    }
    return table;
}

const GenderTable& noun_gender_exceptions() {
    static const GenderTable table = load_dict_reader(noun_gender_path(), "word", "gender");
    return table;
}

const GenderTable& given_names() {
    static const GenderTable table = load_dict_reader(given_names_path(), "name", "gender");
    return table;
}

}  // namespace

std::optional<std::string> suffix_noun_gender(std::string_view raw_word) {
    const std::string word = detail::clean(raw_word);
    for (const auto& rule : rules()) {
        if (matches(rule, word)) return std::string(rule.gender);
    }
    return std::nullopt;
}

std::optional<std::string> TugaLexicon::get_noun_gender(std::string_view word) {
    const std::string lower = detail::py_lower(word);
    const auto* listed = noun_gender_exceptions().find(lower);
    if (listed != nullptr && *listed && !(*listed)->empty()) return *listed;
    return suffix_noun_gender(lower);
}

std::optional<std::string> TugaLexicon::get_given_name_gender(std::string_view name) {
    const auto* listed = given_names().find(detail::clean(name));
    return listed == nullptr ? std::nullopt : *listed;
}

}  // namespace tugalex
