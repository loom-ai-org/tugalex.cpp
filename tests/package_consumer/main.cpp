// Built against an installed tugalex to prove the CMake package and the
// installed data files work. An argument names the data directory that
// data_dir() must find.
#include <filesystem>
#include <tugalex/tugalex.hpp>

int main(int argc, char** argv) {
    const tugalex::TugaLexicon lexicon;
    const auto& map = lexicon.get_ipa_map("NOUN", "lbx");
    const auto* gato = map.find("gato");
    const bool ok = gato != nullptr && *gato == "ɡˈa·tu" &&
                    tugalex::TugaLexicon::get_noun_gender("dia") == "m";
    const bool found = argc < 2 || std::filesystem::equivalent(tugalex::data_dir(), argv[1]);
    return ok && found ? 0 : 1;
}
