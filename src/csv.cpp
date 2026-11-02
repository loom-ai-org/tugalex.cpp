// File reading and a port of CPython 3.12's csv reader (Modules/_csv.c,
// default "excel" dialect: delimiter ",", quotechar '"', doublequote, no
// escapechar, not strict). The parser works on UTF-8 bytes: every character
// it reacts to is ASCII, and UTF-8 never puts an ASCII byte inside a
// multi-byte sequence.
#include <cstdio>
#include <fstream>
#include <iterator>

#include <zlib.h>

#include "detail.hpp"
#include "tugalex/tugalex.hpp"

namespace tugalex::detail {

namespace {

// Python's open(): a path with a NUL byte is a ValueError, not a missing file.
std::string read_bytes(const std::string& path) {
    if (path.find('\0') != std::string::npos) throw std::invalid_argument("embedded null byte");
    std::ifstream in(path, std::ios::binary);
    if (!in) throw FileNotFoundError("No such file or directory: '" + path + "'");
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The zlib messages for a fault that Python's gzip module finds itself and
// reports as BadGzipFile. Every other inflate fault is Python's zlib.error.
bool gzip_format_fault(std::string_view message) {
    for (std::string_view known : {"incorrect header check", "unknown compression method", "unknown header flags set",
                                   "header crc mismatch", "incorrect data check", "incorrect length check"}) {
        if (message == known) return true;
    }
    return false;
}

// Python's gzip reader: one or more members; NUL bytes may pad the file after
// a member; any other byte where a member must start is BadGzipFile; input
// that ends inside a member is EOFError. An empty file is empty text.
std::string gunzip(const std::string& path) {
    const std::string in = read_bytes(path);
    std::string out;
    size_t pos = 0;
    for (bool first = true;; first = false) {
        if (!first) {
            while (pos < in.size() && in[pos] == '\0') ++pos;
        }
        if (pos == in.size()) break;
        if (in.size() - pos < 2 || in[pos] != '\x1f' || in[pos + 1] != '\x8b') {
            throw DataError("BadGzipFile", "Not a gzipped file: " + path);
        }
        z_stream stream{};
        if (inflateInit2(&stream, 16 + MAX_WBITS) != Z_OK) throw std::runtime_error("zlib inflateInit2 failed");
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data() + pos));
        stream.avail_in = static_cast<uInt>(in.size() - pos);
        char buffer[1 << 16];
        int rc;
        do {
            stream.next_out = reinterpret_cast<Bytef*>(buffer);
            stream.avail_out = sizeof buffer;
            rc = inflate(&stream, Z_NO_FLUSH);
            out.append(buffer, sizeof buffer - stream.avail_out);
        } while (rc == Z_OK);
        const std::string message = stream.msg != nullptr ? stream.msg : "";
        pos = in.size() - stream.avail_in;
        inflateEnd(&stream);
        if (rc == Z_STREAM_END) continue;
        if (rc == Z_BUF_ERROR) {
            throw DataError("EOFError", "Compressed file ended before the end-of-stream marker was reached: " + path);
        }
        if (rc == Z_DATA_ERROR && gzip_format_fault(message)) throw DataError("BadGzipFile", message + ": " + path);
        throw DataError("zlib.error", message + ": " + path);
    }
    return out;
}

enum class State { StartRecord, StartField, InField, InQuotedField, QuoteInQuotedField, EatCrnl };

class Parser {
public:
    std::vector<Row> rows;

    void line(std::string_view text) {
        for (char c : text) process(c);
        end_of_line();
        // Python returns a record once a line leaves the parser at the start
        // of a record; an empty line is the record [].
        if (state_ == State::StartRecord) {
            rows.push_back(std::move(fields_));
            fields_.clear();
        }
    }

    // End of input: Python returns a pending record only when a field holds
    // characters or a quoted field is open.
    void finish() {
        if (!field_.empty() || state_ == State::InQuotedField) {
            save_field();
            rows.push_back(std::move(fields_));
        }
    }

private:
    void save_field() {
        fields_.push_back(std::move(field_));
        field_.clear();
    }

    void process(char c) {
        const bool newline = c == '\n' || c == '\r';
        switch (state_) {
            case State::StartRecord:
                if (newline) {
                    state_ = State::EatCrnl;
                    break;
                }
                state_ = State::StartField;
                [[fallthrough]];
            case State::StartField:
                if (newline) {
                    save_field();
                    state_ = State::EatCrnl;
                } else if (c == '"') {
                    state_ = State::InQuotedField;
                } else if (c == ',') {
                    save_field();
                } else {
                    field_.push_back(c);
                    state_ = State::InField;
                }
                break;
            case State::InField:
                if (newline) {
                    save_field();
                    state_ = State::EatCrnl;
                } else if (c == ',') {
                    save_field();
                    state_ = State::StartField;
                } else {
                    field_.push_back(c);
                }
                break;
            case State::InQuotedField:
                if (c == '"') {
                    state_ = State::QuoteInQuotedField;
                } else {
                    field_.push_back(c);
                }
                break;
            case State::QuoteInQuotedField:
                if (c == '"') {
                    field_.push_back(c);
                    state_ = State::InQuotedField;
                } else if (c == ',') {
                    save_field();
                    state_ = State::StartField;
                } else if (newline) {
                    save_field();
                    state_ = State::EatCrnl;
                } else {
                    field_.push_back(c);
                    state_ = State::InField;
                }
                break;
            case State::EatCrnl:
                if (!newline) {
                    throw std::runtime_error(
                        "new-line character seen in unquoted field - do you need to open the file with "
                        "newline=''?");
                }
                break;
        }
    }

    // The EOL sentinel Python feeds after every line.
    void end_of_line() {
        switch (state_) {
            case State::StartRecord:
                break;
            case State::StartField:
            case State::InField:
            case State::QuoteInQuotedField:
                save_field();
                state_ = State::StartRecord;
                break;
            case State::InQuotedField:
                break;
            case State::EatCrnl:
                state_ = State::StartRecord;
                break;
        }
    }

    State state_ = State::StartRecord;
    Row fields_;
    std::string field_;
};

}  // namespace

// Python's os.path.exists: false for a path with a NUL byte.
bool file_exists(const std::string& path) {
    if (path.find('\0') != std::string::npos) return false;
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    std::fclose(file);
    return true;
}

std::string read_text(const std::string& path, bool gzip) {
    std::string bytes = gzip ? gunzip(path) : read_bytes(path);
    if (!valid_utf8(bytes)) throw DataError("UnicodeDecodeError", "'utf-8' codec can't decode " + path);
    return bytes;
}

std::string translate_newlines(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            out.push_back('\n');
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

std::vector<std::string_view> lines_of(std::string_view text, bool universal) {
    std::vector<std::string_view> out;
    size_t start = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n') {
            out.push_back(text.substr(start, i + 1 - start));
            start = i + 1;
        } else if (universal && text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out.push_back(text.substr(start, i + 1 - start));
            start = i + 1;
        }
    }
    if (start < text.size()) out.push_back(text.substr(start));
    return out;
}

std::vector<Row> csv_rows(const std::vector<std::string_view>& lines) {
    Parser parser;
    for (auto line : lines) parser.line(line);
    parser.finish();
    return std::move(parser.rows);
}

}  // namespace tugalex::detail
