# Parity with the Python package

This library is held at byte parity with the Python `tugalex` release named in
`kPythonReference` (`include/tugalex/tugalex.hpp`). The test `parity` checks that
claim. It repeats every call recorded in `tests/goldens/lexicon.jsonl.gz` and
compares the bytes of each result. The file holds 374,034 calls.

## Public surface

The Python package defines 35 public names: 8 at module level and 27 members of
`TugaLexicon` (18 class attributes and 9 instance attributes). The version
constants are left out of that count. Each name maps to C++ as shown below.

| Python | C++ |
| --- | --- |
| `IPA_MAP`, `SYLLABLE_MAP`, `WORD_REPLACEMENT_MAP` | `IpaMap`, `SyllableMap`, `WordReplacementMap` |
| a `dict` (insertion order) | `OrderedMap<V>` |
| `NOUN_GENDER_PATH`, `GIVEN_NAMES_PATH` | `noun_gender_path()`, `given_names_path()` |
| `NOUN_GENDER_SUFFIXES` | `NOUN_GENDER_SUFFIXES`, an array of `SuffixRule` |
| `suffix_noun_gender` | `suffix_noun_gender` |
| `TugaLexicon(dictionary_path=None)` | `TugaLexicon(std::string_view dictionary_path = {})` |
| `dictionary_path`, `ao_pt_path`, `ao_br_path`, `homographs_path`, `archaisms_path` | methods of the same names |
| `ao_pt`, `ao_br`, `homographs`, `archaic_words` | methods of the same names |
| `ipa`, `syllables`, `regions` | methods of the same names (`regions` is a sorted `std::set`) |
| `lang_to_region` | `lang_to_region` |
| `get_phonemes`, `get_syllables`, `get`, `get_wordlist` | the same names (`get` returns `LexiconEntry`) |
| `get_ipa_map` | `get_ipa_map`, cached per (POS, region) |
| `possible_postags`, `AO1990`, `silent_p_words`, `voiced_u_words` | methods of the same names |
| `get_noun_gender`, `get_given_name_gender` | static methods of the same names |
| `normalize_ao1900`, `reverse_ao1900_pt`, `reverse_ao1900_br` | the same names |
| `version.VERSION_*` | `kPythonReference` |

`TugaLexicon` defines no special method except `__init__`. Its results are Python
dicts, lists and sets, which map to C++ as follows:

| Python | C++ |
| --- | --- |
| `len(d)`, `key in d`, `d[key]`, `d.get(key)` on a dict | `size()`, `contains(key)`, `at(key)` (throws `std::out_of_range`), `find(key)` (a pointer, or `nullptr`) |
| iteration over a dict | iteration over `OrderedMap`, in the same order |
| a list | `std::vector`, `size()` counts the elements |
| a set | `std::set`, sorted by code point |

No public function takes an index or returns the length of a string.

The C++ library also has `data_dir()` and `set_data_dir()`. The Python package
finds its data next to its source file. The C++ library uses an install
location, and you can override it. The noun-gender and given-name tables are
cached for each path. Python caches them once, with `lru_cache`, and reads the
module path only on the first call. In C++, a change of the data directory
takes effect at the next call.

Python's exceptions map as follows:

- `KeyError` becomes `std::out_of_range`.
- `ValueError` becomes `std::invalid_argument`, with the same message.
- `FileNotFoundError` becomes `tugalex::FileNotFoundError`.
- An error in a data file becomes `tugalex::DataError`. Its `python_type()`
  gives the name of the Python exception: `UnicodeDecodeError` for a byte that
  is not UTF-8, `EOFError` for a truncated gzip file, `BadGzipFile` for a file
  that is not gzip or has bytes after the last member, and `zlib.error` for
  deflate data that is not valid.

A path that contains a NUL character is a missing file, as in Python's
`os.path.exists`. The golden checks the type of each exception, and it checks
the message of each `ValueError`.

A gzip data file can have two faults: one in its gzip stream and one in its
content. Python decompresses, decodes and parses the file while it reads it.
Thus Python raises the fault that it reads first. The C++ library decompresses
the whole file before it decodes it, so the gzip fault always wins. Two
examples:

- A truncated file with a byte that is not UTF-8 near its start. Python raises
  `UnicodeDecodeError`. C++ raises `DataError` with `python_type()`
  `EOFError`.
- A gender table with no `name` column and a damaged end. Python raises
  `KeyError` at the first data row. C++ raises `DataError` with
  `python_type()` `BadGzipFile` or `EOFError`.

Both languages reject such a file. Only the type of the exception differs, and
no returned value differs.

These parts are not ported:

- The build scripts in the source repository (`scripts/`). The wheel does not
  include them, and they build the data files. They do not read them.
- The `__main__` demonstration block.
- The csv module's field size limit (131,072 characters). Python raises
  `csv.Error` for a longer field. The C++ reader accepts the field.
- Two checks of the gzip header. Python ignores the reserved flag bits and does
  not verify the header CRC (`FHCRC`). zlib rejects both, and the C++ library
  raises `BadGzipFile` for them.

## Behaviour kept on purpose

The port copies these behaviours of the Python code. The goldens check each one.

- The regional dictionary is read through Python's `csv` reader, with its
  quoting rules. Universal-newline translation and the `#` line filter come
  first, so a quoted field can hide a `#` line. An open quote swallows the rest
  of the file.
- The homograph and archaism tables use a plain `split(",")` after all quotes
  are deleted.
- The AO1990 tables are read through the `csv` reader with no line-end
  translation, and the first row is skipped. A row that does not have two
  fields is skipped. Each old spelling in the first field maps to all the new
  spellings of the second field.
- A `|` in the phones becomes `·`. The syllables are split on a space or a `|`.
- Words and regions are stripped and lowercased with the full Unicode rules,
  and the Greek final-sigma rule is included. A repeated word keeps its first
  position and takes its last value.
- `get_syllables` and `get_phonemes` strip and lowercase the word. An unknown
  region raises `ValueError` before any lookup.
- The two gender tables are read through `csv.DictReader`. A missing column
  raises `KeyError` only when the file has a data row, because the columns are
  read from each row. A row too short to hold the key cannot be found, and a
  row too short to hold the value gives `None`. When the header has the same
  name twice, the last column wins.
- `get_ipa_map` replaces each word's reading with the homograph table's reading
  for the POS. This occurs in every region, the Brazilian regions too.
- The suffix rules keep the lookbehind `(?<!ã)os?$`. Python's `$` also matches
  before a final `"\n"`.

## Unicode

Case folding, `str.strip()` and `str.split()` use tables made from Python
itself (`tools/gen_unicode_tables.py`). The header of `src/unicode_tables.inc`
records the Python and Unicode versions.

Every string argument is UTF-8. Each public function first decodes its
arguments as Python's `bytes.decode("utf-8", "replace")` does. Each maximal
invalid subpart becomes one U+FFFD, and the byte that breaks a sequence starts
the next one. Thus `E2 82 41` becomes U+FFFD followed by `A`.

The data files are read as Python reads them. Each file is strict UTF-8, and a
byte that is not valid makes the load fail. A BOM is kept as U+FEFF. The
dictionary and the four small tables have their line ends translated: `\r\n`
and `\r` become `\n`. The two gender tables keep their line ends for the csv
reader (`newline=""`). A missing final newline changes nothing.

## Goldens

`tools/dump_goldens.py` runs against the pinned Python release and writes one
compact JSON line per call. A large result, such as a full regional map, is
stored as the SHA-256 of `json.dumps(value)` together with its length. The C++
test writes the same JSON and hashes it. Thus the full maps are checked, and a
sample alone is not sufficient.

| Corpus | Lines | Source |
| --- | ---: | --- |
| tables | 114 | each small table, the region orders, `ipa`, `syllables`, `get_wordlist` and `get_ipa_map` for every region and every POS, hashed |
| sweeps | 78 | `get_phonemes` and `get_syllables` for all 59,335 sweep words in each region and POS, hashed, and the gender and AO1990 functions over the same words |
| tests | 8,714 | every string literal in tugalex `tests/` |
| homographs | 37,439 | every homograph, archaism and AO1990 entry through each method, and the dictionary words written with capitals |
| genders | 235,974 | every noun-gender exception and given name as written, lowercased and uppercased, and each suffix rule in context |
| sample | 34,098 | seeded random lookups, case and whitespace variants, and sentences |
| unicode | 31,110 | every code point with a lowercase mapping, whitespace status or category Mn/Lt, in context |
| edge | 13,226 | hand-built arguments for each method: empty and whitespace strings, unknown words, regions, POS tags and dialect codes, and odd letter cases |
| raw | 7,140 | 34 byte sequences, valid and invalid UTF-8, in 7 contexts, given as hex to each method that takes a string, against the shipped dictionary and against a fixture whose keys hold U+FFFD |
| fixtures | 5,571 | the dictionaries in `tests/fixtures/`, which exercise the csv and loader quirks, and gzip and UTF-8 files that are not valid |
| datadirs | 570 | the gender lookups against each data directory in `tests/fixtures/`: other line ends, missing and duplicate columns, short rows, damaged gzip and a NUL in the path |

The sweep words are the sorted union of the wordlist of each region, the
homographs, the archaisms and both sides of AO1990. The C++ test builds the same
list through the public API.

To regenerate the goldens, run this with the reference release installed:

```
python -I tools/dump_goldens.py --out tests/goldens/lexicon.jsonl.gz \
    --fixtures tests/fixtures --tests-dir <tugalex>/tests
```
