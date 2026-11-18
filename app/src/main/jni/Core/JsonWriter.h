#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>

namespace JsonWriter {

inline std::string EscapeString(const char* str) {
    if (!str) return "";
    std::string out;
    out.reserve(strlen(str) + 16);
    while (*str) {
        char c = *str++;
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

} // namespace JsonWriter
