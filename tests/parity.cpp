// Byte parity with the Python tugalex. Each golden line holds a call and
// Python's result as [lexicon, method, args, result]; this repeats the call in
// C++, writes the result the way json.dumps does, and compares the bytes.
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "json_line.hpp"
#include "sha256.hpp"
#include "tugalex/tugalex.hpp"

namespace tl = tugalex;
using json_line::quote;

namespace {

std::string js(const std::string& value);
std::string js(const std::optional<std::string>& value);
std::string js(const std::set<std::string>& value);
std::string js(const tl::LexiconEntry& value);
template <class T>
std::string js(const std::vector<T>& value);
template <class V>
std::string js(const tl::OrderedMap<V>& value);

std::string js(const std::string& value) { return quote(value); }
std::string js(const std::optional<std::string>& value) { return value ? quote(*value) : "null"; }
std::string js(const std::set<std::string>& value) { return js(std::vector<std::string>(value.begin(), value.end())); }
std::string js(const tl::LexiconEntry& value) {
    return "{\"syllables\":" + js(value.syllables) + ",\"phonemes\":" + js(value.phonemes) + "}";
}
template <class T>
std::string js(const std::vector<T>& value) {
    std::string out = "[";
    for (size_t i = 0; i < value.size(); ++i) out += (i ? "," : "") + js(value[i]);
    return out + "]";
}
template <class V>
std::string js(const tl::OrderedMap<V>& value) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, item] : value) {
        out += (first ? "" : ",") + quote(key) + ":" + js(item);
        first = false;
    }
    return out + "}";
}

template <class T>
std::string digest(const T& value) {
    return "{\"sha256\":\"" + sha256::hex(js(value)) + "\",\"len\":" + std::to_string(value.size()) + "}";
}

// Python's exceptions as the dumper records them.
std::string guarded(const std::function<std::string()>& run) {
    try {
        return run();
    } catch (const tl::FileNotFoundError&) {
        return "{\"error\":\"FileNotFoundError\"}";
    } catch (const tl::DataError& e) {
        return "{\"error\":" + quote(e.python_type()) + "}";
    } catch (const std::invalid_argument& e) {
        return "{\"error\":\"ValueError\",\"message\":" + quote(e.what()) + "}";
    } catch (const std::out_of_range&) {
        return "{\"error\":\"KeyError\"}";
    }
}

// A value written in full when its JSON is short, else as a digest.
template <class T>
std::string sized(const T& value) {
    std::string text = js(value);
    return text.size() < 20000 ? text : digest(value);
}

std::string basename(const std::string& path) { return path.substr(path.rfind('/') + 1); }

std::vector<std::string> sweep_words(const tl::TugaLexicon& lex) {
    std::set<std::string> words;
    for (const auto& item : lex.syllables()) {
        for (auto& word : lex.get_wordlist(item.first)) words.insert(std::move(word));
    }
    for (const auto& item : lex.homographs()) words.insert(item.first);
    for (const auto& [old_word, new_word] : lex.archaic_words()) {
        words.insert(old_word);
        words.insert(new_word);
    }
    for (const auto& [old_word, new_words] : lex.AO1990()) {
        words.insert(old_word);
        words.insert(new_words.begin(), new_words.end());
    }
    return {words.begin(), words.end()};
}

class Runner {
public:
    explicit Runner(std::string tests_dir) : tests_dir_(std::move(tests_dir)) {}

    std::string run(const std::string& name, const std::string& method, const std::vector<std::string>& a) {
        // "raw:<method>" passes each hex argument to <method> as raw bytes.
        if (method.rfind("raw:", 0) == 0) {
            std::vector<std::string> bytes;
            for (const auto& hexed : a) {
                std::string raw;
                for (size_t i = 0; i + 1 < hexed.size(); i += 2) {
                    raw.push_back(static_cast<char>(std::stoi(hexed.substr(i, 2), nullptr, 16)));
                }
                bytes.push_back(std::move(raw));
            }
            return run(name, method.substr(4), bytes);
        }
        // "datadir:<dir>" reads the gender tables from tests/fixtures/<dir>.
        if (name.rfind("datadir:", 0) == 0) {
            tl::set_data_dir(tests_dir_ + "/fixtures/" + name.substr(8));
            std::string out = guarded([&] {
                if (method == "get_noun_gender") return js(tl::TugaLexicon::get_noun_gender(a.at(0)));
                if (method == "get_given_name_gender") return js(tl::TugaLexicon::get_given_name_gender(a.at(0)));
                throw std::runtime_error("unknown datadir method " + method);
            });
            tl::set_data_dir("");
            return out;
        }
        const tl::TugaLexicon& lex = lexicon(name);
        using L = tl::TugaLexicon;
        if (method == "paths") {
            return js(std::vector<std::string>{basename(lex.dictionary_path()), basename(lex.ao_pt_path()),
                                               basename(lex.ao_br_path()), basename(lex.homographs_path()),
                                               basename(lex.archaisms_path()), basename(tl::noun_gender_path()),
                                               basename(tl::given_names_path())});
        }
        if (method == "NOUN_GENDER_SUFFIXES") {
            std::vector<std::vector<std::string>> rules;
            for (const auto& rule : tl::NOUN_GENDER_SUFFIXES) {
                rules.push_back({std::string(rule.gender), std::string(rule.pattern)});
            }
            return js(rules);
        }
        if (method == "homographs") return js(lex.homographs());
        if (method == "archaic_words") return js(lex.archaic_words());
        if (method == "ao_pt") return digest(lex.ao_pt());
        if (method == "ao_br") return digest(lex.ao_br());
        if (method == "AO1990") return digest(lex.AO1990());
        if (method == "silent_p_words") return js(lex.silent_p_words());
        if (method == "voiced_u_words") return js(lex.voiced_u_words());
        if (method == "ipa_regions" || method == "syllable_regions") {
            return guarded([&] {
                std::vector<std::string> keys;
                if (method == "ipa_regions") {
                    for (const auto& item : lex.ipa()) keys.push_back(item.first);
                } else {
                    for (const auto& item : lex.syllables()) keys.push_back(item.first);
                }
                return js(keys);
            });
        }
        if (method == "regions") return guarded([&] { return js(lex.regions()); });
        if (method == "possible_postags") return guarded([&] { return digest(lex.possible_postags()); });
        if (method == "ipa") return guarded([&] { return digest(lex.ipa().at(a.at(0))); });
        if (method == "syllables") return guarded([&] { return digest(lex.syllables().at(a.at(0))); });
        if (method == "ipa_full") return guarded([&] { return sized(lex.ipa()); });
        if (method == "syllables_full") return guarded([&] { return sized(lex.syllables()); });
        if (method == "get_wordlist") return guarded([&] { return digest(lex.get_wordlist(a.at(0))); });
        if (method == "get_ipa_map") return guarded([&] { return digest(lex.get_ipa_map(a.at(0), a.at(1))); });
        if (method == "lang_to_region") return guarded([&] { return js(lex.lang_to_region(a.at(0))); });

        if (method == "get_phonemes") return guarded([&] { return js(lex.get_phonemes(a.at(0), a.at(1), a.at(2))); });
        if (method == "get_syllables") return guarded([&] { return js(lex.get_syllables(a.at(0), a.at(1))); });
        if (method == "get") return guarded([&] { return js(lex.get(a.at(0), a.at(1), a.at(2))); });
        if (method == "normalize_ao1900") return js(lex.normalize_ao1900(a.at(0)));
        if (method == "reverse_ao1900_pt") return js(lex.reverse_ao1900_pt(a.at(0)));
        if (method == "reverse_ao1900_br") return js(lex.reverse_ao1900_br(a.at(0)));
        if (method == "get_noun_gender") return js(L::get_noun_gender(a.at(0)));
        if (method == "suffix_noun_gender") return js(tl::suffix_noun_gender(a.at(0)));
        if (method == "get_given_name_gender") return js(L::get_given_name_gender(a.at(0)));

        if (method.rfind("sweep_", 0) == 0) return sweep(lex, method, a);
        throw std::runtime_error("unknown method " + method);
    }

private:
    std::string sweep(const tl::TugaLexicon& lex, const std::string& method, const std::vector<std::string>& a) {
        if (words_.empty()) words_ = sweep_words(lex);
        if (method == "sweep_words") return digest(words_);
        auto each = [&](auto fn) {
            return guarded([&] {
                std::vector<decltype(fn(words_[0]))> out;
                out.reserve(words_.size());
                for (const auto& word : words_) out.push_back(fn(word));
                return digest(out);
            });
        };
        if (method == "sweep_syllables") return each([&](const std::string& w) { return lex.get_syllables(w, a.at(0)); });
        if (method == "sweep_phonemes") {
            return each([&](const std::string& w) { return lex.get_phonemes(w, a.at(0), a.at(1)); });
        }
        if (method == "sweep_noun_gender") return each([](const std::string& w) { return tl::TugaLexicon::get_noun_gender(w); });
        if (method == "sweep_suffix_noun_gender") return each([](const std::string& w) { return tl::suffix_noun_gender(w); });
        if (method == "sweep_normalize_ao1900") return each([&](const std::string& w) { return lex.normalize_ao1900(w); });
        if (method == "sweep_reverse_ao1900_pt") return each([&](const std::string& w) { return lex.reverse_ao1900_pt(w); });
        if (method == "sweep_reverse_ao1900_br") return each([&](const std::string& w) { return lex.reverse_ao1900_br(w); });
        throw std::runtime_error("unknown sweep " + method);
    }

    const tl::TugaLexicon& lexicon(const std::string& name) {
        auto& slot = lexicons_[name];
        if (!slot) {
            slot = name.empty() ? std::make_unique<tl::TugaLexicon>()
                                : std::make_unique<tl::TugaLexicon>(tests_dir_ + "/fixtures/" + name);
        }
        return *slot;
    }

    std::string tests_dir_;
    std::map<std::string, std::unique_ptr<tl::TugaLexicon>> lexicons_;
    std::vector<std::string> words_;
};

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: tugalex_parity <tests dir>\n";
        return 2;
    }
    const std::string dir = argv[1];
    const auto lines = json_line::read_gz_lines(dir + "/goldens/lexicon.jsonl.gz");
    if (lines.empty()) {
        std::cerr << "empty golden file\n";
        return 1;
    }
    const long expected = json_line::header_count(lines[0]);
    Runner runner(dir);
    long checked = 0, mismatches = 0;
    std::map<std::string, long> by_source;
    for (size_t i = 1; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        size_t pos = 1;  // past the opening '['
        const std::string name = json_line::unquote(line, pos);
        ++pos;  // ','
        const std::string method = json_line::unquote(line, pos);
        pos += 2;  // ",["
        std::vector<std::string> args;
        while (line.at(pos) != ']') {
            if (line[pos] == ',') ++pos;
            args.push_back(json_line::unquote(line, pos));
        }
        std::string got = "[" + quote(name) + "," + quote(method) + ",[";
        for (size_t k = 0; k < args.size(); ++k) got += (k ? "," : "") + quote(args[k]);
        got += "]," + runner.run(name, method, args) + "]";
        ++checked;
        if (got != line) ++by_source[name.empty() ? "(shipped data)" : name];
        if (got != line && ++mismatches <= 20) {
            std::cerr << "MISMATCH\n  python: " << line.substr(0, 600) << "\n  c++:    " << got.substr(0, 600)
                      << "\n";
        }
    }
    for (const auto& [source, count] : by_source) std::cout << "mismatches in " << quote(source) << ": " << count << "\n";
    std::cout << "goldens: " << expected << " in, " << checked << " checked, " << mismatches << " mismatches\n";
    return (checked == expected && mismatches == 0) ? 0 : 1;
}
