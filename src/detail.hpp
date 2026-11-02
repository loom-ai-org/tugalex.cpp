// Internal helpers: Python's str semantics on UTF-8, file reading, and a port
// of the csv module's reader.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace tugalex::detail {

using U32 = std::u32string;

// Decode UTF-8 as Python's bytes.decode("utf-8", "replace") does.
U32 from_utf8(std::string_view text);
std::string to_utf8(std::u32string_view text);
// Strict UTF-8, as Python's "utf-8" codec accepts it.
bool valid_utf8(std::string_view text);
// The text as Python sees it after decoding with errors="replace".
std::string clean(std::string_view text);

// Python str.strip() with no argument: removes str.isspace() code points.
std::string py_strip(std::string_view text);
// Python str.lower(): full Unicode lowercase mapping, final sigma included.
std::string py_lower(std::string_view text);
// Python str.split() with no argument.
std::vector<std::string> py_split(std::string_view text);
// Python str.split(sep, maxsplit) with a one-byte separator; maxsplit < 0 means no limit.
std::vector<std::string> split_on(std::string_view text, char sep, int maxsplit = -1);
// Python str.replace(old, new).
std::string replace_all(std::string_view text, std::string_view from, std::string_view to);

bool file_exists(const std::string& path);
// The file's bytes, gunzipped when `gzip` is set. Throws FileNotFoundError
// when the file is missing and std::runtime_error when the bytes are not
// strict UTF-8 (Python's UnicodeDecodeError).
std::string read_text(const std::string& path, bool gzip);
// Python's universal-newline translation on read: "\r\n" and "\r" become "\n".
std::string translate_newlines(std::string_view text);
// Python's line iteration over a text file: each line keeps its terminator.
// With `universal` set, "\r", "\n" and "\r\n" all end a line (newline="");
// otherwise only "\n" does (text already translated).
std::vector<std::string_view> lines_of(std::string_view text, bool universal);

// csv.reader with the default "excel" dialect, fed one line at a time.
using Row = std::vector<std::string>;
std::vector<Row> csv_rows(const std::vector<std::string_view>& lines);

}  // namespace tugalex::detail
