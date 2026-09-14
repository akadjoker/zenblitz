#ifndef ZEN_CODEGEN_SUPPORT_HPP
#define ZEN_CODEGEN_SUPPORT_HPP

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

inline std::string zen_str(int64_t value) { return std::to_string(value); }
inline std::string zen_str(bool value) { return std::to_string(static_cast<int64_t>(value)); }
inline std::string zen_str(double value)
{
    if (std::isnan(value)) return "NaN";
    if (std::isinf(value)) return value > 0 ? "Infinity" : "-Infinity";

    const int digits = 6, exponent_negative = -4, exponent_positive = 8;
    char buffer[64];
    std::string result;
    int decimal;
    bool negative = value < 0;
    double absolute = std::fabs(value);

    if (absolute == 0.0)
    {
        result = "000000";
        decimal = 0;
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%.5e", absolute);
        result += buffer[0];
        int index = 2;
        for (; buffer[index] && buffer[index] != 'e'; ++index) result += buffer[index];
        decimal = std::atoi(buffer + index + 1) + 1;
    }

    if (decimal <= exponent_negative + 1 || decimal > exponent_positive)
    {
        std::snprintf(buffer, sizeof(buffer), "%.6g", value);
        return buffer;
    }
    if (decimal <= 0)
    {
        result = "0." + std::string(static_cast<size_t>(-decimal), '0') + result;
        decimal = 1;
    }
    else if (decimal < digits)
        result = result.substr(0, static_cast<size_t>(decimal)) + "." + result.substr(static_cast<size_t>(decimal));
    else
    {
        result += std::string(static_cast<size_t>(decimal - digits), '0') + ".0";
        decimal += decimal - digits;
    }

    int end = static_cast<int>(result.length());
    while (--end > decimal + 1 && result[static_cast<size_t>(end)] == '0') {}
    result.resize(static_cast<size_t>(end + 1));
    return negative ? "-" + result : result;
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
inline int64_t zen_round_int(double value) { return (int64_t)std::nearbyint(value); }
inline void zen_runtime_error(const std::string &msg);

inline int64_t zen_div(int64_t a, int64_t b)
{
    /* the VM's OP_IDIV raises this; without the message and with a bare
       exit the two disagree on stderr while agreeing on stdout, which the
       parity harness cannot see */
    if (b == 0) zen_runtime_error("Division by zero");
    return a / b;
}
inline int64_t zen_mod(int64_t a, int64_t b)
{
    if (b == 0) zen_runtime_error("Division by zero");
    return a % b;
}
struct ZenDataValue
{
    int kind;
    int64_t i;
    double f;
    std::string s;
};
static ZenDataValue *z_data = 0;
static int64_t z_data_ptr = 0;
static int64_t z_gosub_stack[256] __attribute__((unused));
static int64_t z_gosub_sp __attribute__((unused)) = 0;
inline int64_t z_read_int()
{
    ZenDataValue &v = z_data[z_data_ptr++];
    if (v.kind == 1) return zen_round_int(v.f);
    if (v.kind == 2) return zen_int(v.s);
    return v.i;
}
inline double z_read_float()
{
    ZenDataValue &v = z_data[z_data_ptr++];
    if (v.kind == 1) return v.f;
    if (v.kind == 2) return zen_float(v.s);
    return static_cast<double>(v.i);
}
inline std::string z_read_str()
{
    ZenDataValue &v = z_data[z_data_ptr++];
    if (v.kind == 2) return v.s;
    if (v.kind == 1) return zen_str(v.f);
    return zen_str(v.i);
}
inline void zen_print(const std::string &value)
{
    std::fputs(value.c_str(), stdout);
    std::fputc('\n', stdout);
}
inline void zen_write(const std::string &value)
{
    std::fputs(value.c_str(), stdout);
}
inline int64_t zen_millisecs()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void zen_end()
{
    std::fflush(stdout);
    std::exit(0);
}
inline void zen_runtime_error(const std::string &msg)
{
    std::fflush(stdout);
    std::fprintf(stderr, "[runtime error] %s\n", msg.c_str());
    std::exit(1);
}

#endif
