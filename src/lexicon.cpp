// TugaLexicon: the regional dictionary, the homographs, the archaisms and the
// AO1990 spelling tables.
#include <algorithm>
#include <functional>

#include "detail.hpp"
#include "tugalex/tugalex.hpp"

namespace tugalex {

namespace {

const std::map<std::string, std::string, std::less<>>& dialect_regions() {
    static const std::map<std::string, std::string, std::less<>> table = {
        {"pt-PT", "lbx"}, {"pt-BR", "rjx"}, {"pt-AO", "lda"}, {"pt-MZ", "mpx"}, {"pt-TL", "dli"},
    };
    return table;
}

bool contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

// The lines of a small table, read as Python's f.read().split("\n").
std::vector<std::string> table_lines(const std::string& path) {
    return detail::split_on(detail::translate_newlines(detail::read_text(path, false)), '\n');
}

// _load_archaisms and _load_homographs: the header line is skipped, quotes
// are deleted, then the line is split on its first two commas.
template <class Store>
void load_three_columns(const std::string& path, Store store) {
    const auto lines = table_lines(path);
    for (size_t i = 1; i < lines.size(); ++i) {
        if (!contains(lines[i], ",")) continue;
        const auto parts = detail::split_on(detail::replace_all(lines[i], "\"", ""), ',', 2);
        if (parts.size() != 3) continue;
        store(parts);
    }
}

// _load_ao: csv.reader over the file (newline=""), header row skipped. A row
// with other than two fields is skipped. Either field may list spellings
// separated by commas; each old spelling maps to all the new ones.
WordReplacementMap load_ao(const std::string& path) {
    if (!detail::file_exists(path)) throw FileNotFoundError(path);
    const std::string text = detail::read_text(path, false);
    const auto rows = detail::csv_rows(detail::lines_of(text, true));
    WordReplacementMap ao;
    for (size_t r = 1; r < rows.size(); ++r) {
        const auto& row = rows[r];
        if (row.size() != 2) continue;
        std::vector<std::string> new_words;
        for (const auto& word : detail::split_on(row[1], ',')) new_words.push_back(detail::py_strip(word));
        for (const auto& old_word : detail::split_on(row[0], ',')) ao.assign(detail::py_strip(old_word), new_words);
    }
    return ao;
}

std::string join(const std::vector<std::string>& words) {
    std::string out;
    for (size_t i = 0; i < words.size(); ++i) {
        if (i) out += ' ';
        out += words[i];
    }
    return out;
}

std::string reverse_with(const std::unordered_map<std::string, std::string>& reverse, std::string_view sentence) {
    auto words = detail::py_split(sentence);
    for (auto& word : words) {
        const auto it = reverse.find(word);
        if (it != reverse.end()) word = it->second;
    }
    return join(words);
}

void build_reverse(const WordReplacementMap& ao, std::unordered_map<std::string, std::string>& reverse) {
    for (const auto& [old_word, new_words] : ao) {
        for (const auto& new_word : new_words) reverse[new_word] = old_word;
    }
}

}  // namespace

TugaLexicon::TugaLexicon(std::string_view dictionary_path) {
    const std::string dir = data_dir();
    dictionary_path_ = dictionary_path.empty() ? dir + "/regional_dict.csv.gz" : std::string(dictionary_path);
    ao_pt_path_ = dir + "/acordo_ortografico_pt_PT.csv";
    ao_br_path_ = dir + "/acordo_ortografico_pt_BR.csv";
    homographs_path_ = dir + "/heterophonic_homographs.csv";
    archaisms_path_ = dir + "/archaisms.csv";

    ao_pt_ = load_ao(ao_pt_path_);
    ao_br_ = load_ao(ao_br_path_);
    load_three_columns(homographs_path_, [this](const std::vector<std::string>& parts) {
        homographs_.setdefault(detail::py_strip(parts[0])).assign(detail::py_strip(parts[1]),
                                                                  detail::py_strip(parts[2]));
    });
    load_three_columns(archaisms_path_, [this](const std::vector<std::string>& parts) {
        archaic_words_.assign(detail::py_strip(parts[0]), detail::py_strip(parts[1]));
    });
}

// _load_lang_map: Python's csv.reader over the lines that do not start with
// "#". The first row is the header. Rows with fewer than four fields are
// skipped; a repeated (region, word) keeps its first position and its last value.
const TugaLexicon::LangMap& TugaLexicon::lang_map() const {
    std::call_once(lang_once_, [this] {
        if (!detail::file_exists(dictionary_path_)) throw FileNotFoundError(dictionary_path_);
        const bool gzip = dictionary_path_.size() >= 3 &&
                          dictionary_path_.compare(dictionary_path_.size() - 3, 3, ".gz") == 0;
        const std::string text = detail::translate_newlines(detail::read_text(dictionary_path_, gzip));
        std::vector<std::string_view> lines;
        for (auto line : detail::lines_of(text, false)) {
            if (line.empty() || line.front() != '#') lines.push_back(line);
        }
        const auto rows = detail::csv_rows(lines);

        auto map = std::make_unique<LangMap>();
        for (size_t r = 1; r < rows.size(); ++r) {
            const auto& parts = rows[r];
            if (parts.size() < 4) continue;
            const std::string word = detail::py_lower(detail::py_strip(parts[0]));
            const std::string region = detail::py_lower(detail::py_strip(parts[3]));
            std::string phonemes = detail::py_strip(detail::replace_all(parts[1], "|", "·"));
            map->regions.insert(region);
            map->ipa.setdefault(region).setdefault(word).assign("", std::move(phonemes));
            const std::string syllables = detail::replace_all(detail::py_strip(parts[2]), " ", "|");
            map->syllables.setdefault(region).assign(word, detail::split_on(syllables, '|'));
        }
        lang_ = std::move(map);
    });
    return *lang_;
}

const IpaMap& TugaLexicon::ipa() const { return lang_map().ipa; }
const SyllableMap& TugaLexicon::syllables() const { return lang_map().syllables; }
const std::set<std::string>& TugaLexicon::regions() const { return lang_map().regions; }

std::string TugaLexicon::lang_to_region(std::string_view raw_lang) const {
    const std::string lang = detail::clean(raw_lang);
    const auto it = dialect_regions().find(lang);
    if (it == dialect_regions().end()) throw std::invalid_argument("Unsupported dialect: " + lang);
    return it->second;
}

std::optional<std::string> TugaLexicon::get_phonemes(std::string_view word, std::string_view raw_pos,
                                                     std::string_view raw_region) const {
    const std::string pos = detail::clean(raw_pos), region = detail::clean(raw_region);
    const auto* entries = ipa().find(region);
    if (entries == nullptr) throw std::invalid_argument("Unsupported dialect: " + region);

    std::string key = detail::py_lower(detail::py_strip(word));
    if (const auto* modern = archaic_words_.find(key)) key = *modern;

    if (const auto* readings = homographs_.find(key)) {
        if (const auto* reading = readings->find(pos)) return *reading;
    }

    const auto* entry = entries->find(key);
    if (entry == nullptr) return std::nullopt;
    // entry.get(pos) or entry.get("")
    if (const auto* reading = entry->find(pos); reading != nullptr && !reading->empty()) return *reading;
    if (const auto* reading = entry->find("")) return *reading;
    return std::nullopt;
}

std::vector<std::string> TugaLexicon::get_syllables(std::string_view word, std::string_view raw_region) const {
    const std::string region = detail::clean(raw_region);
    const auto* words = syllables().find(region);
    if (words == nullptr) throw std::invalid_argument("Unsupported dialect: " + region);
    const auto* found = words->find(detail::py_lower(detail::py_strip(word)));
    return found == nullptr ? std::vector<std::string>{} : *found;
}

LexiconEntry TugaLexicon::get(std::string_view word, std::string_view pos, std::string_view region) const {
    LexiconEntry entry;
    entry.syllables = get_syllables(word, region);
    entry.phonemes = get_phonemes(word, pos, region);
    return entry;
}

std::vector<std::string> TugaLexicon::get_wordlist(std::string_view raw_region) const {
    const std::string region = detail::clean(raw_region);
    const auto* words = syllables().find(region);
    if (words == nullptr) throw std::invalid_argument("Unsupported dialect: " + region);
    std::vector<std::string> out;
    out.reserve(words->size());
    for (const auto& item : *words) out.push_back(item.first);
    std::sort(out.begin(), out.end());  // byte order on UTF-8 is code-point order
    return out;
}

const OrderedMap<std::string>& TugaLexicon::get_ipa_map(std::string_view raw_pos,
                                                        std::string_view raw_region) const {
    const std::string pos = detail::clean(raw_pos), region = detail::clean(raw_region);
    const auto* entries = ipa().find(region);
    if (entries == nullptr) throw std::invalid_argument("Unsupported dialect: " + region);

    std::lock_guard<std::mutex> lock(ipa_map_mutex_);
    auto& slot = ipa_maps_[{pos, region}];
    if (slot) return *slot;

    auto table = std::make_unique<OrderedMap<std::string>>();
    for (const auto& [word, entry] : *entries) {
        // The lexicon is POS-free; POS only selects homograph overrides.
        const std::string* value = entry.find(pos);
        if (value == nullptr || value->empty()) value = entry.find("");
        if (value != nullptr && !value->empty()) table->assign(word, *value);
    }
    // Every homograph with a reading for `pos` overrides, whatever the region.
    for (const auto& [word, readings] : homographs_) {
        if (const auto* reading = readings.find(pos)) table->assign(word, *reading);
    }
    slot = std::move(table);
    return *slot;
}

const OrderedMap<std::vector<std::string>>& TugaLexicon::possible_postags() const {
    std::call_once(postags_once_, [this] {
        OrderedMap<std::vector<std::string>> tags;
        for (const auto& [word, entry] : ipa().at("lbx")) {
            std::vector<std::string> keys;
            for (const auto& item : entry) keys.push_back(item.first);
            tags.assign(word, std::move(keys));
        }
        for (const auto& [word, readings] : homographs_) {
            std::vector<std::string> keys;
            for (const auto& item : readings) keys.push_back(item.first);
            tags.assign(word, std::move(keys));
        }
        postags_ = std::move(tags);
    });
    return postags_;
}

const WordReplacementMap& TugaLexicon::AO1990() const {
    std::call_once(ao1990_once_, [this] {
        for (const auto& [old_word, new_words] : ao_pt_) ao1990_.assign(old_word, new_words);
        for (const auto& [old_word, new_words] : ao_br_) ao1990_.assign(old_word, new_words);
    });
    return ao1990_;
}

const std::set<std::string>& TugaLexicon::silent_p_words() const {
    std::call_once(silent_p_once_, [this] {
        for (std::string_view cluster : {"mpc", "mpç", "mpt"}) {
            for (const auto& [old_word, new_words] : AO1990()) {
                for (const auto& new_word : new_words) {
                    if (contains(old_word, cluster) && !contains(new_word, cluster)) silent_p_.insert(old_word);
                }
            }
        }
    });
    return silent_p_;
}

const std::set<std::string>& TugaLexicon::voiced_u_words() const {
    std::call_once(voiced_u_once_, [this] {
        for (const auto& [old_word, new_words] : ao_br_) {
            if (!contains(old_word, "ü")) continue;
            for (const auto& new_word : new_words) voiced_u_.insert(new_word);
        }
    });
    return voiced_u_;
}

std::string TugaLexicon::normalize_ao1900(std::string_view sentence) const {
    auto words = detail::py_split(sentence);
    const auto& ao = AO1990();
    for (auto& word : words) {
        if (const auto* replacement = ao.find(word)) word = replacement->front();
    }
    return join(words);
}

std::string TugaLexicon::reverse_ao1900_pt(std::string_view sentence) const {
    std::call_once(reverse_pt_once_, [this] { build_reverse(ao_pt_, reverse_pt_); });
    return reverse_with(reverse_pt_, sentence);
}

std::string TugaLexicon::reverse_ao1900_br(std::string_view sentence) const {
    std::call_once(reverse_br_once_, [this] { build_reverse(ao_br_, reverse_br_); });
    return reverse_with(reverse_br_, sentence);
}

}  // namespace tugalex
