"""Dump the Python tugalex's output as the C++ port's parity goldens.

Each golden line is a compact JSON array [lexicon, method, args, result]:
  lexicon  "" for the shipped dictionary, else a file name in tests/fixtures/
  method   the public name called, or a "sweep_*" name for a hashed sweep
  args     the call's arguments
  result   the return value; {"error": <Python exception>} when it raises
           (ValueError adds its "message"); {"sha256", "len"} for a large
           value, hashed over json.dumps(value) with the same separators

The C++ parity test recomputes each line and compares the bytes.

Corpora:
  tables     every small table, both region-key orders, every regional map
             (ipa, syllables, get_ipa_map for each POS, get_wordlist) hashed
  sweeps     get_phonemes and get_syllables for every sweep word, each region
             and each POS, hashed per (POS, region); the sweep words are the
             sorted union of every region's wordlist, the homographs, the
             archaisms and both sides of AO1990
  tests      the inputs of the tugalex test suite (--tests-dir)
  homographs every homograph, archaism and AO1990 entry through each method
  genders    every noun-gender exception and every given name, as written,
             lowercased and uppercased; the suffix-rule coverage set
  sample     seeded random lookups and sentences
  unicode    case-mapping and whitespace probes
  fixtures   hand-written dictionaries in tests/fixtures/ for the csv quirks

Usage:
  python -I tools/dump_goldens.py --out tests/goldens/lexicon.jsonl.gz \\
      --fixtures tests/fixtures --tests-dir <tugalex>/tests
"""
import argparse
import ast
import csv
import gzip
import hashlib
import importlib.metadata
import io
import json
import os
import pathlib
import random
import sys
import unicodedata
import zlib

import tugalex
from tugalex import TugaLexicon

REGIONS_EXTRA = ["zzz", "LBX", ""]
POS_EXTRA = ["", "X", "noun"]


def dumps(value):
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"))


def digest(value):
    text = dumps(value)
    return {"sha256": hashlib.sha256(text.encode("utf-8")).hexdigest(), "len": len(value)}


class Raised(dict):
    """The record of an exception; a dict so that it dumps as JSON."""


def call(fn, *args):
    try:
        return fn(*args)
    except UnicodeDecodeError:  # a ValueError, so it goes first
        return Raised(error="UnicodeDecodeError")
    except ValueError as e:
        return Raised(error="ValueError", message=str(e))
    except KeyError:
        return Raised(error="KeyError")
    except FileNotFoundError:
        return Raised(error="FileNotFoundError")
    except EOFError:
        return Raised(error="EOFError")
    except gzip.BadGzipFile:
        return Raised(error="BadGzipFile")
    except zlib.error:
        return Raised(error="zlib.error")


def is_error(value):
    return isinstance(value, Raised)


def sized(value):
    """The value when its JSON is short, else its digest; errors pass through."""
    if is_error(value) or len(dumps(value).encode("utf-8")) < 20000:
        return value
    return digest(value)


def hashed(fn, *args):
    value = call(fn, *args)
    return value if is_error(value) else digest(value)


def sorted_set(fn):
    value = call(fn)
    return value if is_error(value) else sorted(value)


class Writer:
    """Collects golden lines; a line already written by an earlier call is dropped."""

    def __init__(self):
        self.lines = []
        self.seen = set()
        self.calls = {}
        self.kept = {}

    def add(self, corpus, lexicon, method, args, result):
        line = dumps([lexicon, method, list(args), result])
        self.calls[corpus] = self.calls.get(corpus, 0) + 1
        if line not in self.seen:
            self.seen.add(line)
            self.lines.append(line)
            self.kept[corpus] = self.kept.get(corpus, 0) + 1


def lookups(out, corpus, lex, name, word, poses, regions):
    for region in regions:
        out.add(corpus, name, "get_syllables", [word, region], call(lex.get_syllables, word, region))
        for pos in poses:
            out.add(corpus, name, "get_phonemes", [word, pos, region],
                    call(lex.get_phonemes, word, pos, region))
            out.add(corpus, name, "get", [word, pos, region], call(lex.get, word, pos, region))


def spelling(out, corpus, lex, name, sentence):
    for method in ("normalize_ao1900", "reverse_ao1900_pt", "reverse_ao1900_br"):
        out.add(corpus, name, method, [sentence], call(getattr(lex, method), sentence))


def genders(out, corpus, word):
    out.add(corpus, "", "get_noun_gender", [word], TugaLexicon.get_noun_gender(word))
    out.add(corpus, "", "suffix_noun_gender", [word], tugalex.suffix_noun_gender(word))
    out.add(corpus, "", "get_given_name_gender", [word], TugaLexicon.get_given_name_gender(word))


def table_records(out, corpus, lex, name, poses, regions):
    out.add(corpus, name, "ipa_regions", [], call(lambda: list(lex.ipa)))
    out.add(corpus, name, "syllable_regions", [], call(lambda: list(lex.syllables)))
    out.add(corpus, name, "regions", [], sorted_set(lambda: lex.regions))
    out.add(corpus, name, "possible_postags", [], hashed(lambda: lex.possible_postags))
    for region in regions:
        out.add(corpus, name, "ipa", [region], hashed(lambda: lex.ipa[region]))
        out.add(corpus, name, "syllables", [region], hashed(lambda: lex.syllables[region]))
        out.add(corpus, name, "get_wordlist", [region], hashed(lex.get_wordlist, region))
        for pos in poses:
            out.add(corpus, name, "get_ipa_map", [pos, region], hashed(lex.get_ipa_map, pos, region))


def sweep_words(lex):
    words = set()
    for region in lex.syllables:
        words.update(lex.get_wordlist(region))
    words.update(lex.homographs)
    words.update(lex.archaic_words)
    words.update(lex.archaic_words.values())
    for old, news in lex.AO1990.items():
        words.add(old)
        words.update(news)
    return sorted(words)


def homograph_poses(lex):
    return sorted({pos for readings in lex.homographs.values() for pos in readings})


def test_literals(tests_dir):
    for path in sorted(pathlib.Path(tests_dir).glob("test_*.py")):
        for node in ast.walk(ast.parse(path.read_text(encoding="utf-8"))):
            if isinstance(node, ast.Constant) and isinstance(node.value, str):
                if len(node.value) <= 40 and "\n" not in node.value:
                    yield node.value


def suffix_words():
    # Every NOUN_GENDER_SUFFIXES alternative in the contexts that decide it:
    # bare, after a letter, after ã (the lookbehind), before a final newline
    # (Python's `$`), before other line ends, and in other letter cases.
    endings = ["ção", "ções", "são", "sões", "dade", "dades", "tude", "tudes", "gem", "gens",
               "eza", "ezas", "ise", "ises", "o", "os", "or", "ores", "ume", "umes", "a", "as"]
    for ending in endings:
        for template in ("{}", "x{}", "ã{}", "Ã{}", "a{}", "{}\n", "x{}\n", "ã{}\n", "{}\n\n",
                         "{}\r", "{}\r\n", "{} ", " {}", "{}x", "{}s", "c{}", "ç{}", "s{}", "aç{}"):
            word = template.format(ending)
            yield word
            yield word.upper()
            yield word.title()
    for word in ("", "\n", "ão", "ãos", "ão\n", "aão", "ãoo", "ãoos", "çã", "ç", "õ", "ão",
                 "ação", "nação", "cidade", "x" * 50 + "a"):
        yield word


def unicode_probes():
    # Every code point whose lowercase or whitespace status matters, plus the
    # Greek final-sigma contexts.
    for cp in range(0x110000):
        if 0xD800 <= cp <= 0xDFFF:
            continue
        c = chr(cp)
        if c.lower() != c or c.isspace() or unicodedata.category(c) in ("Mn", "Lt"):
            yield c
    for text in ("ΣΑΣ", "ΑΣ", "Σ", "ΑΣΑ", "ΆΣ", "ΑΣ́", "Α.Σ", "ΑΣ'", "'Σ", "aΣ b", "ΑΣ1"):
        yield text


# Byte sequences that are not UTF-8, next to valid ones. Python decodes them
# with errors="replace": one U+FFFD per maximal invalid subpart, and the byte
# that breaks a sequence starts the next one.
RAW_SEQUENCES = [
    "e28241", "e282", "c080", "c1bf", "eda080", "edbfbf", "f0808080", "f4908080", "f5", "ff", "fe",
    "80", "bf", "8080", "c2", "c241", "e0a0", "e080", "e0a041", "f09080", "f0908041", "f0", "e2", "c3",
    "c341", "e2c3a7", "f4", "f48f", "ed9fbf", "efbfbd", "e282ac", "c3a7c3a3", "f09f9880", "c3a3c3",
]
RAW_CONTEXTS = ["{}", "63617361{}", "{}63617361", "434153{}41", "6e61{}c3a7c3a36f", "20{}20", "c38dc391{}"]


def raw_inputs():
    for sequence in RAW_SEQUENCES:
        for context in RAW_CONTEXTS:
            yield context.format(sequence)


def raw_records(out, lex, name=""):
    # Each argument is hex; Python decodes it and calls the method.
    def text(hexed):
        return bytes.fromhex(hexed).decode("utf-8", "replace")

    def add(method, fn, *hexes):
        out.add("raw", name, "raw:" + method, list(hexes), call(fn, *(text(h) for h in hexes)))

    noun, lbx, para = "4e4f554e", "6c6278", "70617261"
    for word in raw_inputs():
        add("get_phonemes", lex.get_phonemes, word, noun, lbx)
        add("get_phonemes", lex.get_phonemes, para, word, lbx)
        add("get_phonemes", lex.get_phonemes, word, noun, word)
        add("get_syllables", lex.get_syllables, word, lbx)
        add("get", lex.get, word, noun, lbx)
        add("get_wordlist", lex.get_wordlist, word)
        add("get_ipa_map", lambda p, r: digest(lex.get_ipa_map(p, r)), word, lbx)
        add("get_ipa_map", lambda p, r: digest(lex.get_ipa_map(p, r)), noun, word)
        add("lang_to_region", lex.lang_to_region, word)
        add("get_noun_gender", TugaLexicon.get_noun_gender, word)
        add("suffix_noun_gender", tugalex.suffix_noun_gender, word)
        add("get_given_name_gender", TugaLexicon.get_given_name_gender, word)
        for method in ("normalize_ao1900", "reverse_ao1900_pt", "reverse_ao1900_br"):
            add(method, getattr(lex, method), word + "2061c3a7c3a7c3a36f")


EDGE_WORDS = [
    "", " ", "\t", "\n", "\r\n", "\u00a0", "\u3000", "casa", "Casa", "CASA", " casa ", "casa\n", "\ncasa",
    "zzzz", "para", "PARA", " Para\t", "pelo", "Pelo", "architecto", "ARCHITECTO", " architecto ",
    "acção", "Acção", "ação", "camisa de vénus", "camisa de Vénus", "CAMISA DE VÉNUS", "old_form",
    "new_form", "OLD_FORM", "a", "A", "é", "É", "ΣΑΣ", "İ", "ß", "ﬁ", "casa casa", "\x00", "·",
    "ˈpɐɾɐ", "ɡˈa·tu", "Isabel", "isabel", "ISABEL", "João", "joão", "Darci",
]
EDGE_POS = ["NOUN", "VERB", "ADP", "ADJ", "", "noun", "Noun", " NOUN", "X", "PROPN"]
EDGE_LANGS = ["pt-PT", "pt-BR", "pt-AO", "pt-MZ", "pt-TL", "pt-pt", "PT-PT", " pt-PT", "pt-PT ",
              "pt_PT", "en-US", "pt", "", "lbx", "pt-PT\n"]


def edge_records(out, lex, regions):
    queried = regions + ["zzz", "LBX", " lbx", "lbx ", "", "rjo", "pt-PT"]
    for word in EDGE_WORDS:
        lookups(out, "edge", lex, "", word, EDGE_POS, queried)
        genders(out, "edge", word)
        spelling(out, "edge", lex, "", word)
        spelling(out, "edge", lex, "", f"{word} acção {word}")
    for region in queried:
        out.add("edge", "", "get_wordlist", [region], hashed(lex.get_wordlist, region))
        for pos in EDGE_POS:
            out.add("edge", "", "get_ipa_map", [pos, region], hashed(lex.get_ipa_map, pos, region))
    for lang in EDGE_LANGS:
        out.add("edge", "", "lang_to_region", [lang], call(lex.lang_to_region, lang))


FIXTURES = {
    "quoting.csv": (
        "# provenance line\n"
        "word,phones,syllables,region_code\n"
        "# a comment between rows\n"
        '"ca,sa",ka|za,ca|sa,lbx\n'
        '"dou""ble",d|b,dou ble,LBX\n'
        '"multi\nline",m|l,mu|lti,lbx\n'
        '"open\n# hidden continuation\nend",x,x,rjx\n'
        'a"b,p|q,a b,lbx\n'
        '"x"y,z,z,lbx\n'
        ' Spaced ,  s|p  ,  spa ced  , Lbx \n'
        " Nbsp　,n|b,n b, rjx \n"
        "\x1cSep\x1f,s,s,lbx\n"
        "ÉGUA,ɛ|ɡwɐ,é|gua,lbx\n"
        "égua,E,é|gua,lbx\n"
        "short,row\n"
        "\n"
        "too,many,fields,lbx,extra,more\n"
        ",,,lbx\n"
        "emptyphones,,e|m,lbx\n"
        "pipeonly,|,|,lbx\n"
        "ΣΑΣ,s,s,lbx\n"
        "ὈΔΥΣΣΕΎΣ,o,o,lbx\n"
        "İstanbul,i,i,lbx\n"
        "para,p,p,lbx\n"
        "para,p2,p2,rjx\n"
        "gato,g,g,dli\n"
        "égua,E2,é gua,rjx\n"
        "nul\x00word,n,n,lbx\n"
        "tab\tword,t,t\tx,lbx\n"
        "  ,blank,word,lbx\n"
        '"unterminated,u,u,lbx\n'
        "tail,t,t,lbx\n"
    ),
    "crlf.csv": (
        "# provenance\r\n"
        "word,phones,syllables,region_code\r\n"
        "casa,ka|zɐ,ca|sa,lbx\r\n"
        "mesa,me|zɐ,me|sa,lbx\r"
        '"two\r\nlines",t|l,two lines,lbx\r'
        '"lone\rcr",l|c,lone cr,lbx\n'
        "#hash\r"
        "last,l,l,rjx"
    ),
    "plain.csv.gz": (
        "word,phones,syllables,region_code\n"
        "casa,ka|zɐ,ca|sa,lbx\n"
        "casa,ka|za,ca|sa,rjx\n"
        "Casa,KA,Ca|sa,lbx\n"
        "para,pa|ɾɐ,pa|ra,lbx\n"
    ),
    "gzip_name.csv": "word,phones,syllables,region_code\ncasa,k,c,lbx\n",
    "bom.csv": "﻿word,phones,syllables,region_code\n﻿casa,k,c,lbx\ncasa2,k,c,﻿lbx\n",
    "no_lbx.csv": "word,phones,syllables,region_code\ncasa,k,c,rjx\npara,p,p,rjx\n",
    "header_only.csv": "word,phones,syllables,region_code\n",
    "comments_only.csv": "# one\n# two\n",
    "empty.csv": "",
    "no_newline.csv": "word,phones,syllables,region_code\ncasa,k,c,lbx",
    "swallow.csv": (
        "word,phones,syllables,region_code\n"
        "casa,k,c,lbx\n"
        '"q"",r,r,lbx\n'
        "mesa,m,m,lbx\n"
        'x",y,z,lbx\n'
        "after,a,a,lbx\n"
    ),
    "replacement.csv": (
        "word,phones,syllables,region_code\n"
        "ca\ufffdsa,k,ca|\ufffd|sa,lbx\n"
        "\ufffd,r,\ufffd,lbx\n"
        "\ufffd\ufffdx,r2,x,lbx\n"
        "\ufffda,r3,a,lb\ufffd\n"
        "casa,k,ca|sa,lbx\n"
    ),
    "eof_open_quote.csv": 'word,phones,syllables,region_code\ncasa,k,ca,"',
    "hash_inside.csv": (
        "word,phones,syllables,region_code\n"
        "ca#sa,k#1,c#a,lbx\n"
        "casa, #x,y,lbx\n"
        "#lead,x,x,lbx\n"
        '"#quoted",q,q,lbx\n'
        "mesa,m,m,#rjx\n"
    ),
    "open_quote_end.csv": 'word,phones,syllables,region_code\ncasa,k,c,lbx\n"dangling,x,x,lbx',
}
MISSING_FIXTURE = "missing.csv"
# A NUL byte in the path: Python's os.path.exists is False for it.
NUL_FIXTURES = ["plain.csv.gz\x00.csv", "quoting.csv\x00x.gz"]


def gz(data):
    return gzip.compress(data, compresslevel=9, mtime=0)


def ignorable(c):
    return ("A" + c + "Σ").lower()[-1] == "ς" and ("AΣ" + c).lower()[1] == "ς"


def cased(c):
    return (c + "Σ").lower()[-1] == "ς"


def sigma_rows():
    # The final-sigma rule in context: Σ after a case-ignorable run that
    # follows a cased letter, Σ before one, and Σ after each cased letter.
    rows = ["word,phones,syllables,region_code"]
    for cp in range(0x110000):
        if 0xD800 <= cp <= 0xDFFF or chr(cp) in ',"\r\n':
            continue
        c = chr(cp)
        if ignorable(c):
            rows += [f"Α{c}Σ,k,k,lbx", f"ΑΣ{c}Β,k,k,lbx", f"Α{c}{c}Σ{c},k,k,lbx"]
        elif cased(c):
            rows.append(f"{c}Σ,k,k,lbx")
    return "\n".join(rows) + "\n"


def binary_fixtures():
    text = FIXTURES["plain.csv.gz"].encode("utf-8")
    member = gz(text)
    header = member[:10]
    return {
        "trunc.csv.gz": member[: len(member) // 2],
        "trunc_trailer.csv.gz": member[:-3],
        "garbage.csv.gz": member + b"GARBAGE",
        "padded.csv.gz": member + b"\x00\x00\x00",
        "padded_garbage.csv.gz": member + b"\x00\x00x",
        "leading_nul.csv.gz": b"\x00" + member,
        "members.csv.gz": gz(text[:40]) + gz(text[40:]),
        "plaintext.csv.gz": text,
        "empty.csv.gz": b"",
        "onebyte.csv.gz": b"\x1f",
        "header_only.csv.gz": header[:3],
        "method7.csv.gz": header[:2] + b"\x07" + member[3:],
        "corrupt.csv.gz": header + b"\x07" + b"\x00" * 8,
        "badcrc.csv.gz": member[:-8] + bytes([member[-8] ^ 0xFF]) + member[-7:],
        "badlength.csv.gz": member[:-4] + bytes([member[-4] ^ 0x01]) + member[-3:],
        "bad_utf8.csv": b"word,phones,syllables,region_code\ncasa,k,c,lbx\nca\xe2\x82sa,k,c,lbx\n",
        "bad_utf8.csv.gz": gz(b"word,phones,syllables,region_code\ncasa,k,c,lbx\nca\xffsa,k,c,lbx\n"),
        "sigma.csv.gz": gz(sigma_rows().encode("utf-8")),
    }


def synthetic_names(count):
    return "".join(f"Nome{i:04d},{'mf'[i % 2]}\n" for i in range(count))


# Gender-table directories: (given_names.csv.gz, noun_gender_exceptions.csv.gz)
# contents. None leaves the file out.
def datadirs():
    big = gz(("name,gender\n" + synthetic_names(800)).encode())
    big_noun = gz(("word,gender\n" + synthetic_names(800).replace("Nome", "palavra")).encode())
    return {
        "cr": (gz(b"name,gender\rAna,f\rRui,m\r"), gz(b"word,gender\rcasa,m\rdia,f\r")),
        "crlf": (gz(b"name,gender\r\nAna,f\r\n\"Bo\r\nb\",m\r\n"), gz(b"word,gender\r\ncasa,m\r\n")),
        "noname": (gz(b"nome,gender\n"), gz(b"palavra,gender\n")),
        "noname_rows": (gz(b"nome,gender\nAna,f\n"), gz(b"palavra,gender\ncasa,f\n")),
        "shortrow": (gz(b"gender,name\nm\nf,Ana\n"), gz(b"gender,word\nm\nf,casa\n")),
        "dupcols": (gz(b"name,gender,name\nAna,f\nRui,m,Bob\n"), gz(b"word,gender,word\ncasa,m\ndia,f,mesa\n")),
        "emptyval": (gz(b"name,gender\nAna,\nRui\n"), gz(b"word,gender\ncasa,\ndia\n")),
        "blank_first": (gz(b"\nname,gender\nAna,f\n"), gz(b"\nword,gender\ncasa,m\n")),
        "truncated": (big[: len(big) // 2], big_noun[: len(big_noun) // 2]),
        "plain": (b"name,gender\nAna,f\n", b"word,gender\ncasa,m\n"),
        "garbage": (gz(b"name,gender\nAna,f\n") + b"XX", gz(b"word,gender\ncasa,m\n") + b"XX"),
        "members": (gz(b"name,gender\nAna,f\n") + gz(b"Rui,m\n"), gz(b"word,gender\n") + gz(b"casa,m\n")),
        "badutf8": (gz(b"name,gender\nAn\xe3,f\n"), gz(b"word,gender\nc\xc3sa,m\n")),
        "missing": (None, None),
    }


DATADIR_NUL = "nul\x00dir"
DATADIR_WORDS = ["Ana", "Rui", "Bob", "Bo\r\nb", "name", "gender", "nome", "Nome0001", "Nome0799",
                 "casa", "dia", "mesa", "palavra0002", "m", "f", "", "ana", "CASA", "dias"]


def write_fixtures(directory):
    directory.mkdir(parents=True, exist_ok=True)
    for name, data in binary_fixtures().items():
        (directory / name).write_bytes(data)
    for name, files in datadirs().items():
        for filename, data in zip(("given_names.csv.gz", "noun_gender_exceptions.csv.gz"), files):
            if data is not None:
                (directory / name).mkdir(exist_ok=True)
                (directory / name / filename).write_bytes(data)
    for name, text in FIXTURES.items():
        data = text.encode("utf-8")
        path = directory / name
        if name.endswith(".gz"):
            with open(path, "wb") as raw:
                with gzip.GzipFile(fileobj=raw, mode="wb", mtime=0, filename="") as handle:
                    handle.write(data)
        else:
            path.write_bytes(data)


def datadir_records(out, directory):
    saved = tugalex.GIVEN_NAMES_PATH, tugalex.NOUN_GENDER_PATH
    for name in list(datadirs()) + [DATADIR_NUL]:
        base = os.path.join(directory, name)
        tugalex.GIVEN_NAMES_PATH = os.path.join(base, "given_names.csv.gz")
        tugalex.NOUN_GENDER_PATH = os.path.join(base, "noun_gender_exceptions.csv.gz")
        tugalex._given_names.cache_clear()
        tugalex._noun_gender_exceptions.cache_clear()
        for word in DATADIR_WORDS:
            for method in ("get_given_name_gender", "get_noun_gender"):
                out.add("datadirs", "datadir:" + name, method, [word], call(getattr(TugaLexicon, method), word))
    tugalex.GIVEN_NAMES_PATH, tugalex.NOUN_GENDER_PATH = saved
    tugalex._given_names.cache_clear()
    tugalex._noun_gender_exceptions.cache_clear()


def fixture_records(out, directory):
    names = list(FIXTURES) + list(binary_fixtures()) + [MISSING_FIXTURE] + NUL_FIXTURES
    for name in names:
        lex = TugaLexicon(os.path.join(directory, name))
        regions = call(lambda: sorted(lex.regions))
        regions = [] if is_error(regions) else regions
        queried = regions + [r for r in ("lbx", "zzz") if r not in regions]
        table_records(out, "fixtures", lex, name, ["NOUN", "VERB", "ADP", ""], queried)
        out.add("fixtures", name, "ipa_full", [], sized(call(lambda: lex.ipa)))
        out.add("fixtures", name, "syllables_full", [], sized(call(lambda: lex.syllables)))
        words = {"para", "casa", "égua", "ÉGUA", " casa ", "ca,sa", "zzz"}
        for region in regions:
            words.update(lex.syllables[region])
        if len(words) > 200:  # a probe table: the full maps above cover it
            continue
        for word in sorted(words):
            lookups(out, "fixtures", lex, name, word, ["NOUN", "ADP", ""], queried)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", required=True)
    parser.add_argument("--fixtures", required=True)
    parser.add_argument("--tests-dir")
    parser.add_argument("--sample", type=int, default=4000)
    args = parser.parse_args()

    out = Writer()
    lex = TugaLexicon()
    regions = sorted(lex.regions)
    poses = homograph_poses(lex) + POS_EXTRA
    all_regions = regions + REGIONS_EXTRA

    # tables
    out.add("tables", "", "paths", [], [os.path.basename(p) for p in (
        lex.dictionary_path, lex.ao_pt_path, lex.ao_br_path, lex.homographs_path, lex.archaisms_path,
        tugalex.NOUN_GENDER_PATH, tugalex.GIVEN_NAMES_PATH)])
    out.add("tables", "", "NOUN_GENDER_SUFFIXES", [], [list(rule) for rule in tugalex.NOUN_GENDER_SUFFIXES])
    out.add("tables", "", "homographs", [], lex.homographs)
    out.add("tables", "", "archaic_words", [], lex.archaic_words)
    for method in ("ao_pt", "ao_br", "AO1990"):
        out.add("tables", "", method, [], hashed(lambda: getattr(lex, method)))
    out.add("tables", "", "silent_p_words", [], sorted(lex.silent_p_words))
    out.add("tables", "", "voiced_u_words", [], sorted(lex.voiced_u_words))
    table_records(out, "tables", lex, "", poses, all_regions)
    for lang in ("pt-PT", "pt-BR", "pt-AO", "pt-MZ", "pt-TL", "pt-pt", "PT-PT", "en-US", "pt", "", "lbx"):
        out.add("tables", "", "lang_to_region", [lang], call(lex.lang_to_region, lang))

    # sweeps
    words = sweep_words(lex)
    out.add("sweeps", "", "sweep_words", [], digest(words))
    for region in all_regions:
        out.add("sweeps", "", "sweep_syllables", [region],
                hashed(lambda: [lex.get_syllables(w, region) for w in words]))
        for pos in poses:
            out.add("sweeps", "", "sweep_phonemes", [pos, region],
                    hashed(lambda: [lex.get_phonemes(w, pos, region) for w in words]))
    out.add("sweeps", "", "sweep_noun_gender", [],
            digest([TugaLexicon.get_noun_gender(w) for w in words]))
    out.add("sweeps", "", "sweep_suffix_noun_gender", [],
            digest([tugalex.suffix_noun_gender(w) for w in words]))
    out.add("sweeps", "", "sweep_normalize_ao1900", [], digest([lex.normalize_ao1900(w) for w in words]))
    out.add("sweeps", "", "sweep_reverse_ao1900_pt", [], digest([lex.reverse_ao1900_pt(w) for w in words]))
    out.add("sweeps", "", "sweep_reverse_ao1900_br", [], digest([lex.reverse_ao1900_br(w) for w in words]))

    # tests
    if args.tests_dir:
        for literal in sorted(set(test_literals(args.tests_dir))):
            lookups(out, "tests", lex, "", literal, ["NOUN", "VERB", "ADP"], regions)
            genders(out, "tests", literal)
            spelling(out, "tests", lex, "", literal)
            out.add("tests", "", "lang_to_region", [literal], call(lex.lang_to_region, literal))

    # homographs, archaisms, AO1990, and the raw-case dictionary words
    for word in lex.homographs:
        lookups(out, "homographs", lex, "", word, poses, all_regions)
    for old, new in lex.archaic_words.items():
        lookups(out, "homographs", lex, "", old, ["NOUN", "VERB"], regions + ["zzz"])
        lookups(out, "homographs", lex, "", old.upper(), ["NOUN"], ["lbx"])
    with gzip.open(lex.dictionary_path, "rt", encoding="utf-8") as handle:
        raw = {row[0] for row in csv.reader(line for line in handle if not line.startswith("#"))}
    for word in sorted(w for w in raw if w != w.lower()):
        lookups(out, "homographs", lex, "", word, ["NOUN"], regions)
    for old, news in lex.AO1990.items():
        spelling(out, "homographs", lex, "", old)
        for new in news:
            spelling(out, "homographs", lex, "", new)
    spelling(out, "homographs", lex, "", " ".join(list(lex.AO1990)[:200]))

    # genders
    with gzip.open(tugalex.NOUN_GENDER_PATH, "rt", encoding="utf-8", newline="") as handle:
        nouns = [row["word"] for row in csv.DictReader(handle)]
    with gzip.open(tugalex.GIVEN_NAMES_PATH, "rt", encoding="utf-8", newline="") as handle:
        names = [row["name"] for row in csv.DictReader(handle)]
    seen = set()
    for word in nouns + names + list(suffix_words()):
        for variant in (word, word.lower(), word.upper()):
            if variant not in seen:
                seen.add(variant)
                genders(out, "genders", variant)

    # sample
    rng = random.Random(20261102)
    for _ in range(args.sample):
        word = rng.choice(words)
        variant = rng.choice([word, word.upper(), word.title(), f" {word}\t", f" {word}　",
                              word + "s", word[:-1]])
        lookups(out, "sample", lex, "", variant, [rng.choice(poses)], [rng.choice(all_regions)])
        genders(out, "sample", variant)
        sentence = " ".join(rng.choice(words) for _ in range(rng.randint(0, 8)))
        spelling(out, "sample", lex, "", rng.choice([sentence, sentence.replace(" ", "  \t"),
                                                     f" {sentence}\n"]))

    # unicode
    for probe in unicode_probes():
        for word in (probe + "casa" + probe, "a" + probe + "e", probe + "ba", "CAS" + probe + "A"):
            out.add("unicode", "", "get_phonemes", [word, "NOUN", "lbx"], call(lex.get_phonemes, word, "NOUN", "lbx"))
            out.add("unicode", "", "get_noun_gender", [word], TugaLexicon.get_noun_gender(word))
        out.add("unicode", "", "normalize_ao1900", ["acção" + probe + "óptimo"],
                lex.normalize_ao1900("acção" + probe + "óptimo"))

    edge_records(out, lex, regions)
    raw_records(out, lex)
    write_fixtures(pathlib.Path(args.fixtures))
    # Dictionary keys that hold U+FFFD, which a raw invalid byte decodes to.
    raw_records(out, TugaLexicon(os.path.join(args.fixtures, "replacement.csv")), "replacement.csv")

    # fixtures
    write_fixtures(pathlib.Path(args.fixtures))
    fixture_records(out, args.fixtures)
    datadir_records(out, args.fixtures)

    total = len(out.lines)
    for corpus, count in out.calls.items():
        print(f"{corpus}: {count} in, {out.kept.get(corpus, 0)} out (repeats dropped)", file=sys.stderr)

    versions = {"tugalex": importlib.metadata.version("tugalex")}
    buffer = io.StringIO()
    buffer.write(dumps({"versions": versions, "python": sys.version.split()[0],
                        "unicode": unicodedata.unidata_version, "count": total}) + "\n")
    for line in out.lines:
        buffer.write(line + "\n")
    with open(args.out, "wb") as raw_out:
        with gzip.GzipFile(fileobj=raw_out, mode="wb", mtime=0, filename="") as handle:
            handle.write(buffer.getvalue().encode("utf-8"))
    print(f"total: {total} in, {total} out -> {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
