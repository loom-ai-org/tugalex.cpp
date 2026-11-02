// tugalex: a regional Portuguese pronunciation lexicon with homograph,
// archaism, spelling-reform, given-name and noun-gender data.
//
// C++ port of the Python package of the same name. Every function takes and
// returns UTF-8. Case folding and whitespace stripping follow Python's
// str.lower() and str.strip() exactly. A string argument that is not valid
// UTF-8 is decoded as Python's bytes.decode("utf-8", "replace") would decode it.
#pragma once

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace tugalex {

// The Python release this port is held at byte parity with.
inline constexpr const char* kPythonReference = "2.2.0a1";

// A string-keyed map that iterates in insertion order, as a Python dict does.
// Assigning to a key that exists keeps its position.
template <class V>
class OrderedMap {
public:
    using value_type = std::pair<std::string, V>;
    using const_iterator = typename std::vector<value_type>::const_iterator;

    const V* find(std::string_view key) const {
        const auto at = position(key);
        return at == npos ? nullptr : &items_[at].second;
    }
    const V& at(std::string_view key) const {
        const V* value = find(key);
        if (value == nullptr) throw std::out_of_range(std::string(key));
        return *value;
    }
    bool contains(std::string_view key) const { return position(key) != npos; }
    std::size_t size() const { return items_.size(); }
    bool empty() const { return items_.empty(); }
    const_iterator begin() const { return items_.begin(); }
    const_iterator end() const { return items_.end(); }

    // Python's d.setdefault(key, V()): the value under `key`, inserted last if absent.
    V& setdefault(std::string_view key) {
        auto at = position(key);
        if (at != npos) return items_[at].second;
        items_.emplace_back(std::string(key), V());
        if (!index_.empty() || items_.size() > kLinearLimit) reindex_last();
        return items_.back().second;
    }
    // Python's d[key] = value.
    void assign(std::string_view key, V value) { setdefault(key) = std::move(value); }

private:
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    // Small maps (one POS per word) are searched linearly and carry no index.
    static constexpr std::size_t kLinearLimit = 8;

    std::size_t position(std::string_view key) const {
        if (index_.empty()) {
            for (std::size_t i = 0; i < items_.size(); ++i) {
                if (items_[i].first == key) return i;
            }
            return npos;
        }
        const auto it = index_.find(std::string(key));
        return it == index_.end() ? npos : it->second;
    }
    void reindex_last() {
        if (index_.empty()) {
            for (std::size_t i = 0; i < items_.size(); ++i) index_.emplace(items_[i].first, i);
        } else {
            index_.emplace(items_.back().first, items_.size() - 1);
        }
    }

    std::vector<value_type> items_;
    std::unordered_map<std::string, std::size_t> index_;
};

// POS -> phonemes. The lexicon is POS-free and stores one reading under "".
using PosMap = OrderedMap<std::string>;
// IPA_MAP: region -> word -> POS -> phonemes.
using IpaMap = OrderedMap<OrderedMap<PosMap>>;
// SYLLABLE_MAP: region -> word -> list of syllables.
using SyllableMap = OrderedMap<OrderedMap<std::vector<std::string>>>;
// WORD_REPLACEMENT_MAP: a word mapping such as AO1990 old -> new spellings.
using WordReplacementMap = OrderedMap<std::vector<std::string>>;

// Python's FileNotFoundError: a data file does not exist.
class FileNotFoundError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The directory that holds the data files. The first of these that is set
// wins: set_data_dir(), the TUGALEX_DATA_DIR environment variable, the
// install location compiled into the library. Set it before the first lookup:
// the noun-gender and given-name tables load once per process.
std::string data_dir();
void set_data_dir(std::string path);

// NOUN_GENDER_PATH and GIVEN_NAMES_PATH.
std::string noun_gender_path();
std::string given_names_path();

// NOUN_GENDER_SUFFIXES: (gender, regex) pairs, first match wins. The patterns
// are kept as Python wrote them; suffix_noun_gender runs a hand port of each.
struct SuffixRule {
    std::string_view gender;
    std::string_view pattern;
};
inline constexpr std::array<SuffixRule, 11> NOUN_GENDER_SUFFIXES = {{
    {"f", "ç(?:ão|ões)$"},
    {"f", "s(?:ão|ões)$"},
    {"f", "dades?$"},
    {"f", "tudes?$"},
    {"f", "(?:gem|gens)$"},
    {"f", "ezas?$"},
    {"f", "ises?$"},
    {"m", "(?<!ã)os?$"},
    {"m", "(?:or|ores)$"},
    {"m", "umes?$"},
    {"f", "as?$"},
}};

// The gender that the suffix rules alone give a noun, or nullopt.
std::optional<std::string> suffix_noun_gender(std::string_view word);

// The result of TugaLexicon::get.
struct LexiconEntry {
    std::vector<std::string> syllables;
    std::optional<std::string> phonemes;
};

// The lexicon handler. Construction reads the four small tables. The regional
// dictionary loads on the first call that needs it, once per object.
// Python's KeyError is std::out_of_range, ValueError is std::invalid_argument.
class TugaLexicon {
public:
    // An empty `dictionary_path` selects data_dir()/regional_dict.csv.gz.
    // The dictionary is read as gzip when the path ends in ".gz".
    explicit TugaLexicon(std::string_view dictionary_path = {});
    TugaLexicon(const TugaLexicon&) = delete;
    TugaLexicon& operator=(const TugaLexicon&) = delete;

    const std::string& dictionary_path() const { return dictionary_path_; }
    const std::string& ao_pt_path() const { return ao_pt_path_; }
    const std::string& ao_br_path() const { return ao_br_path_; }
    const std::string& homographs_path() const { return homographs_path_; }
    const std::string& archaisms_path() const { return archaisms_path_; }

    const WordReplacementMap& ao_pt() const { return ao_pt_; }
    const WordReplacementMap& ao_br() const { return ao_br_; }
    // word -> POS -> IPA.
    const OrderedMap<OrderedMap<std::string>>& homographs() const { return homographs_; }
    // archaic spelling -> modern spelling.
    const OrderedMap<std::string>& archaic_words() const { return archaic_words_; }

    const IpaMap& ipa() const;
    const SyllableMap& syllables() const;
    const std::set<std::string>& regions() const;

    // "pt-PT" -> "lbx" and so on; std::invalid_argument for other codes.
    std::string lang_to_region(std::string_view lang) const;

    std::optional<std::string> get_phonemes(std::string_view word, std::string_view pos = "NOUN",
                                            std::string_view region = "lbx") const;
    // The word is used as given: no stripping, no lowercasing.
    std::vector<std::string> get_syllables(std::string_view word, std::string_view region = "lbx") const;
    LexiconEntry get(std::string_view word, std::string_view pos = "NOUN",
                     std::string_view region = "lbx") const;
    std::vector<std::string> get_wordlist(std::string_view region = "lbx") const;
    // Built once per (pos, region); later calls return the same map.
    const OrderedMap<std::string>& get_ipa_map(std::string_view pos = "NOUN",
                                               std::string_view region = "lbx") const;
    const OrderedMap<std::vector<std::string>>& possible_postags() const;

    static std::optional<std::string> get_noun_gender(std::string_view word);
    static std::optional<std::string> get_given_name_gender(std::string_view name);

    const WordReplacementMap& AO1990() const;
    const std::set<std::string>& silent_p_words() const;
    const std::set<std::string>& voiced_u_words() const;

    std::string normalize_ao1900(std::string_view sentence) const;
    std::string reverse_ao1900_pt(std::string_view sentence) const;
    std::string reverse_ao1900_br(std::string_view sentence) const;

private:
    struct LangMap {
        IpaMap ipa;
        SyllableMap syllables;
        std::set<std::string> regions;
    };
    const LangMap& lang_map() const;

    std::string dictionary_path_, ao_pt_path_, ao_br_path_, homographs_path_, archaisms_path_;
    WordReplacementMap ao_pt_, ao_br_;
    OrderedMap<OrderedMap<std::string>> homographs_;
    OrderedMap<std::string> archaic_words_;

    mutable std::once_flag lang_once_, postags_once_, ao1990_once_, silent_p_once_, voiced_u_once_,
        reverse_pt_once_, reverse_br_once_;
    mutable std::unique_ptr<LangMap> lang_;
    mutable OrderedMap<std::vector<std::string>> postags_;
    mutable WordReplacementMap ao1990_;
    mutable std::set<std::string> silent_p_, voiced_u_;
    mutable std::unordered_map<std::string, std::string> reverse_pt_, reverse_br_;
    mutable std::mutex ipa_map_mutex_;
    mutable std::map<std::pair<std::string, std::string>, std::unique_ptr<OrderedMap<std::string>>> ipa_maps_;
};

}  // namespace tugalex
