// Just enough JSON to compare against Python's
// json.dumps(value, ensure_ascii=False, separators=(",", ":")) byte for byte.
#pragma once

#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

#include <zlib.h>

namespace json_line {

inline std::string quote(const std::string& text) {
    std::string out = "\"";
    for (unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out + "\"";
}

inline void append_utf8(std::string& out, unsigned cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decode the JSON string literal starting at line[pos] (which must be '"').
// Advances pos past the closing quote.
inline std::string unquote(const std::string& line, size_t& pos) {
    if (line.at(pos) != '"') throw std::runtime_error("expected a string at " + std::to_string(pos));
    std::string out;
    ++pos;
    auto hex4 = [&](size_t at) { return static_cast<unsigned>(std::stoul(line.substr(at, 4), nullptr, 16)); };
    while (line.at(pos) != '"') {
        char c = line[pos++];
        if (c != '\\') {
            out.push_back(c);
            continue;
        }
        char e = line.at(pos++);
        switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                unsigned cp = hex4(pos);
                pos += 4;
                if (cp >= 0xD800 && cp < 0xDC00 && line.compare(pos, 2, "\\u") == 0) {
                    unsigned low = hex4(pos + 2);
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    pos += 6;
                }
                append_utf8(out, cp);
                break;
            }
            default:
                throw std::runtime_error("bad escape");
        }
    }
    ++pos;
    return out;
}

// Read every line of a gzip-compressed text file.
inline std::vector<std::string> read_gz_lines(const std::string& path) {
    gzFile file = gzopen(path.c_str(), "rb");
    if (file == nullptr) throw std::runtime_error("cannot open " + path);
    std::vector<std::string> lines;
    std::string current;
    char buffer[1 << 16];
    int n;
    while ((n = gzread(file, buffer, sizeof buffer)) > 0) {
        for (int i = 0; i < n; ++i) {
            if (buffer[i] == '\n') {
                lines.push_back(std::move(current));
                current.clear();
            } else {
                current.push_back(buffer[i]);
            }
        }
    }
    gzclose(file);
    if (!current.empty()) lines.push_back(std::move(current));
    return lines;
}

// The leading "count" of a golden header line such as {"...","count":87091}.
inline long header_count(const std::string& header) {
    size_t at = header.rfind("\"count\":");
    if (at == std::string::npos) throw std::runtime_error("golden header has no count");
    return std::stol(header.substr(at + 8));
}

}  // namespace json_line
