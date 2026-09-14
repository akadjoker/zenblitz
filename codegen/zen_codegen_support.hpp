#ifndef ZEN_CODEGEN_SUPPORT_HPP
#define ZEN_CODEGEN_SUPPORT_HPP

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

inline std::string zen_str(int64_t value) { return std::to_string(value); }
inline std::string zen_str(double value)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6g", value);
    std::string result(buffer);
    if (result.find('.') == std::string::npos && result.find('e') == std::string::npos && result.find('E') == std::string::npos)
        result += ".0";
    return result;
}
inline int64_t zen_int(const std::string &value) { return std::strtoll(value.c_str(), 0, 10); }
inline double zen_float(const std::string &value) { return std::strtod(value.c_str(), 0); }
inline int64_t zen_len(const std::string &value) { return static_cast<int64_t>(value.size()); }
inline std::string zen_string(const std::string &value, int64_t count)
{
    std::string result;
    while (count-- > 0) result += value;
    return result;
}
inline double zen_sin(double value) { return std::sin(value * 0.01745329251994329577); }
inline double zen_cos(double value) { return std::cos(value * 0.01745329251994329577); }
inline double zen_tan(double value) { return std::tan(value * 0.01745329251994329577); }
inline double zen_sqr(double value) { return std::sqrt(value); }
inline double zen_floor(double value) { return std::floor(value); }
inline double zen_ceil(double value) { return std::ceil(value); }
inline double zen_exp(double value) { return std::exp(value); }
inline double zen_log(double value) { return std::log(value); }
inline double zen_log10(double value) { return std::log10(value); }
inline std::string zen_left(const std::string &s, int64_t n) { return n < 0 ? std::string() : s.substr(0, static_cast<size_t>(n)); }
inline std::string zen_right(const std::string &s, int64_t n)
{
    if (n < 0) return {};
    size_t count = static_cast<size_t>(n);
    return s.substr(count >= s.size() ? 0 : s.size() - count);
}
inline std::string zen_mid(const std::string &s, int64_t start, int64_t count)
{
    if (start <= 0) return {};
    size_t pos = static_cast<size_t>(start - 1);
    if (pos > s.size()) pos = s.size();
    return count < 0 ? s.substr(pos) : s.substr(pos, static_cast<size_t>(count));
}
inline std::string zen_replace(std::string s, const std::string &from, const std::string &to)
{
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos)
    {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}
inline std::string zen_trim(const std::string &s)
{
    size_t begin = 0, end = s.size();
    while (begin < end && !std::isgraph(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && !std::isgraph(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}
inline std::string zen_lset(std::string s, int64_t size)
{
    if (size < 0) return {};
    if (static_cast<int64_t>(s.size()) > size) return s.substr(0, static_cast<size_t>(size));
    return s + std::string(static_cast<size_t>(size - s.size()), ' ');
}
inline std::string zen_rset(std::string s, int64_t size)
{
    if (size < 0) return {};
    if (static_cast<int64_t>(s.size()) > size) return s.substr(s.size() - static_cast<size_t>(size));
    return std::string(static_cast<size_t>(size - s.size()), ' ') + s;
}
inline int64_t zen_instr(const std::string &s, const std::string &find, int64_t from)
{
    if (from <= 0) return 0;
    size_t pos = s.find(find, static_cast<size_t>(from - 1));
    return pos == std::string::npos ? 0 : static_cast<int64_t>(pos + 1);
}
inline std::string zen_upper(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[i])));
    return s;
}
inline std::string zen_lower(std::string s)
{
    for (size_t i = 0; i < s.size(); ++i) s[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(s[i])));
    return s;
}
inline std::string zen_chr(int64_t n) { return std::string(1, static_cast<char>(n)); }
inline int64_t zen_asc(const std::string &s) { return s.empty() ? -1 : static_cast<unsigned char>(s[0]); }
inline std::string zen_hex(int64_t n)
{
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0') << std::setw(8)
        << static_cast<uint32_t>(n);
    return out.str();
}
inline std::string zen_bin(int64_t n)
{
    std::string out(32, '0');
    uint32_t value = static_cast<uint32_t>(n);
    for (int i = 31; i >= 0; --i, value >>= 1) out[static_cast<size_t>(i)] = (value & 1) ? '1' : '0';
    return out;
}
inline void zen_print(const std::string &value)
{
    std::fputs(value.c_str(), stdout);
    std::fputc('\n', stdout);
}

#endif
