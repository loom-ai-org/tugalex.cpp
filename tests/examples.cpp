// The assertions of the Python test suite whose expected values do not come
// from tugalex itself (Wikidata, MorphoBr, the AO1990 text).
#include <iostream>
#include <stdexcept>
#include <string>

#include "tugalex/tugalex.hpp"

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}
}  // namespace

int main() {
    using tugalex::TugaLexicon;
    const TugaLexicon lex;

    check(lex.lang_to_region("pt-PT") == "lbx" && lex.lang_to_region("pt-BR") == "rjx", "lang_to_region");
    try {
        lex.lang_to_region("en-US");
        check(false, "lang_to_region(en-US) raises");
    } catch (const std::invalid_argument&) {
    }

    check(lex.homographs().at("para").size() == 2, "para has two homograph readings");
    check(lex.get_phonemes("para", "ADP") != lex.get_phonemes("para", "VERB"), "para ADP != VERB");
    check(lex.archaic_words().at("architecto") == "arquiteto", "architecto");
    check(lex.normalize_ao1900("acção óptimo") == "ação ótimo", "normalize_ao1900");
    check(lex.reverse_ao1900_pt(lex.normalize_ao1900("acção")) == "acção", "reverse_ao1900_pt");
    check(lex.normalize_ao1900("casa") == "casa", "unknown word passes through");
    check(!lex.silent_p_words().empty() && !lex.voiced_u_words().empty(), "phenomena sets");

    check(lex.get_ipa_map("NOUN", "lbx").at("gato").find("ɡ") != std::string::npos, "gato has IPA g");
    check(lex.get_phonemes("paciente", "NOUN", "lbx") == "pɐ·sjˈẽt", "paciente");
    check(lex.get_phonemes("textos", "NOUN", "lbx") == std::nullopt, "textos is not an entry");
    check(&lex.get_ipa_map("NOUN", "rjx") == &lex.get_ipa_map("NOUN", "rjx"), "get_ipa_map is cached");

    for (auto [word, gender] : {std::pair{"lei", "f"}, {"dia", "m"}, {"problema", "m"}, {"nação", "f"},
                                {"carro", "m"}, {"artista", "mf"}, {"Lei", "f"}, {"DIA", "m"}}) {
        check(TugaLexicon::get_noun_gender(word) == gender, std::string("noun gender of ") + word);
    }
    for (const char* word : {"", "123", "zzk"}) {
        check(TugaLexicon::get_noun_gender(word) == std::nullopt, std::string("no gender for ") + word);
    }
    for (auto [name, gender] : {std::pair{"Maria", "f"}, {"João", "m"}, {"Darci", "mf"}, {"Rui", "m"}}) {
        check(TugaLexicon::get_given_name_gender(name) == gender, std::string("given name ") + name);
    }
    check(TugaLexicon::get_given_name_gender("joão") == std::nullopt, "given names are case-sensitive");

    if (failures == 0) std::cout << "examples: all passed\n";
    return failures == 0 ? 0 : 1;
}
