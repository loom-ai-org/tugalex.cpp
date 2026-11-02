// Built against an installed tugalex to prove the CMake package and the
// installed data files work.
#include <tugalex/tugalex.hpp>

int main() {
    const tugalex::TugaLexicon lexicon;
    const auto& map = lexicon.get_ipa_map("NOUN", "lbx");
    const auto* gato = map.find("gato");
    const bool ok = gato != nullptr && *gato == "ɡˈa·tu" &&
                    tugalex::TugaLexicon::get_noun_gender("dia") == "m";
    return ok ? 0 : 1;
}
