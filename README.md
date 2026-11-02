# tugalex.cpp

A C++17 port of [tugalex](https://github.com/TigreGotico/tugalex), the regional
Portuguese pronunciation lexicon. It gives the same output as the Python package,
byte for byte: see [PARITY.md](PARITY.md). It ships the same data files.

```cpp
#include <tugalex/tugalex.hpp>

const tugalex::TugaLexicon lex;
lex.get_phonemes("gato");                    // "ɡˈa·tu" (region "lbx")
lex.get_syllables("gato", "rjx");            // {"ga", "to"}
const auto& rio = lex.get_ipa_map("NOUN", "rjx");
rio.find("gato");                            // pointer to the reading, or nullptr
lex.normalize_ao1900("acção óptimo");        // "ação ótimo"
tugalex::TugaLexicon::get_noun_gender("dia");          // "m"
tugalex::TugaLexicon::get_given_name_gender("Isabel"); // "f"
```

The lexicon has 53,150 words in six regions: `lbx` (Lisbon), `rjx` (Rio de
Janeiro), `spx` (São Paulo), `lda` (Luanda), `mpx` (Maputo) and `dli` (Dili).
`lang_to_region` maps `pt-PT`, `pt-BR`, `pt-AO`, `pt-MZ` and `pt-TL` to these
codes.

## Loading and cost

The constructor reads the four small tables: AO1990 for Portugal and Brazil,
the homographs and the archaisms. The regional dictionary loads on the first
call that needs it, once for each `TugaLexicon`. `get_ipa_map` builds each
(POS, region) map once and then returns the same reference. The noun-gender and
given-name tables load once for each data path. All const methods are safe to call
from more than one thread.

The maps are `tugalex::OrderedMap`. This type iterates in the same order as the
Python dict, and it looks up a key in constant time.

## Data files

The library finds its data in the first of these locations that is set:

1. `tugalex::set_data_dir(path)`.
2. The `TUGALEX_DATA_DIR` environment variable.
3. The install location, `<prefix>/share/tugalex`.

A `TugaLexicon` reads its tables when it is made, so set the location before
you make one. A `TugaLexicon` can also read another
dictionary: give its path to the constructor. A path that ends in `.gz` is read
as gzip.

## Build

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
cmake --install build
```

The library needs zlib. To use it from another CMake project:

```cmake
find_package(tugalex REQUIRED)
target_link_libraries(app PRIVATE tugalex::tugalex)
```

## Licence

Apache-2.0, the same licence as the Python package and its data.
