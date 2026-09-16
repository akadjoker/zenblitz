#ifndef ZEN_CODEGEN_SUPPORT_HPP
#define ZEN_CODEGEN_SUPPORT_HPP

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

inline void zen_runtime_error(const std::string &msg);

/* ================= handles =================
   File, directory and timer handles come out of the VM's own table
   (libzen/src/bb_handles.h): the low 16 bits are the slot index and the high
   15 a generation counter, so the first handle is 65536 - slot 0, generation
   1 - and a closed slot comes back, on reuse, carrying the next generation.
   A Blitz program compares handles against small numbers and stores them
   through PokeInt/WriteInt, so the native tables have to answer the same
   numbers; a 1-based counter would print 1 where the VM prints 65536. */
template <class T>
struct ZenHandles
{
    enum { INDEX_BITS = 16, MAX_SLOTS = 1 << INDEX_BITS, GEN_MASK = 0x7FFF };

    struct Slot
    {
        T value;
        unsigned generation;
    };

    std::vector<Slot> slots;
    std::vector<unsigned> free_slots;

    int64_t add(T value)
    {
        if (!value) return 0;   /* a null object marks a free slot */
        unsigned index;
        if (!free_slots.empty())
        {
            index = free_slots.back();
            free_slots.pop_back();
            slots[index].value = value;
        }
        else
        {
            if (slots.size() >= (size_t)MAX_SLOTS) return 0;
            index = (unsigned)slots.size();
            Slot slot;
            slot.value = value;
            slot.generation = 1;
            slots.push_back(slot);
        }
        return (int64_t)(((slots[index].generation & GEN_MASK) << INDEX_BITS) | index);
    }

    T get(int64_t handle) const
    {
        unsigned index;
        return live(handle, &index) ? slots[index].value : T();
    }

    void remove(int64_t handle)
    {
        unsigned index;
        if (!live(handle, &index)) return;
        slots[index].value = T();
        /* a new generation, so handles to the old occupant stop matching */
        slots[index].generation = (slots[index].generation + 1) & GEN_MASK;
        if (slots[index].generation == 0) slots[index].generation = 1;
        free_slots.push_back(index);
    }

private:
    bool live(int64_t handle, unsigned *out) const
    {
        if (handle <= 0) return false;
        const unsigned index = (unsigned)handle & (MAX_SLOTS - 1);
        const unsigned generation = ((unsigned)handle >> INDEX_BITS) & GEN_MASK;
        if (index >= slots.size()) return false;
        const Slot &slot = slots[index];
        if (!slot.value || slot.generation != generation) return false;
        *out = index;
        return true;
    }
};

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
inline std::string zen_left(const std::string &s, int64_t n)
{
    /* "parameter must be positive", exactly where runtime3d's c_Left checks
       it: the VM aborts on a negative count and so has to this. */
    if (n < 0) zen_runtime_error("parameter must be positive");
    return s.substr(0, static_cast<size_t>(n));
}
inline std::string zen_right(const std::string &s, int64_t n)
{
    if (n < 0) zen_runtime_error("parameter must be positive");
    size_t count = static_cast<size_t>(n);
    return s.substr(count >= s.size() ? 0 : s.size() - count);
}
inline std::string zen_mid(const std::string &s, int64_t start, int64_t count)
{
    if (start <= 0) zen_runtime_error("parameter must be greater than 0");
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
    if (size < 0) zen_runtime_error("parameter must be positive");
    if (static_cast<int64_t>(s.size()) > size) return s.substr(0, static_cast<size_t>(size));
    return s + std::string(static_cast<size_t>(size - s.size()), ' ');
}
inline std::string zen_rset(std::string s, int64_t size)
{
    if (size < 0) zen_runtime_error("parameter must be positive");
    if (static_cast<int64_t>(s.size()) > size) return s.substr(s.size() - static_cast<size_t>(size));
    return std::string(static_cast<size_t>(size - s.size()), ' ') + s;
}
inline int64_t zen_instr(const std::string &s, const std::string &find, int64_t from)
{
    if (from <= 0) zen_runtime_error("parameter must be greater than 0");
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
/* Every Print/Write flushes, exactly like the VM's backend_stdio (sb_print and
   sb_write do too, for the same reason): stdout goes block-buffered as soon as
   it is redirected to a pipe - the editor's Output panel, or the file
   tools/codegen_probe.sh diffs the two runtimes through - and output left in
   the buffer would surface at exit instead, after every warning the program
   wrote to stderr in the meantime. That alone made the snd2 probe differ from
   the VM on nothing but ordering. */
inline void zen_print(const std::string &value)
{
    std::fputs(value.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}
inline void zen_write(const std::string &value)
{
    std::fputs(value.c_str(), stdout);
    std::fflush(stdout);
}

/* Warnings and errors reach stderr in the order the program produced them,
   which only holds if stdout is emptied first - the VM's sb_log does the same
   flush before writing the message. */
inline void zen_stderr_line(const std::string &message)
{
    std::fflush(stdout);
    std::fputs(message.c_str(), stderr);
    std::fputc('\n', stderr);
}
inline int64_t zen_millisecs()
{
    return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

/* ================= core commands (both flavours) =================
   Everything from here on is what libzen's bb_cmds.cpp publishes as the core
   command set: strings (above), random, trigonometry, date/time, the process
   commands and the fixed-rate timers. They live in this header, not in
   zen_engine_sdk.h, because a 2D program has every one of them. */

/* Random: Blitz's own LCG, the same constants and the same float steps as
   runtime3d's c_Rnd/c_Rand, so a seed produces the same sequence as the VM
   and as the original Blitz3D. std::rand would not: the sequences differ, and
   samples seed and reuse Rnd across frames. */
static int z_rnd_state = 0x1234;
static inline float z_rnd_next()
{
    const int rnd_a = 48271, rnd_m = 2147483647, rnd_q = 44488, rnd_r = 3399;
    z_rnd_state = rnd_a * (z_rnd_state % rnd_q) - rnd_r * (z_rnd_state / rnd_q);
    if (z_rnd_state < 0) z_rnd_state += rnd_m;
    return (float)(z_rnd_state & 65535) / 65536.0f + (.5f / 65536.0f);
}
inline double zen_rnd(double from, double to = 0.0)
{
    const float a = (float)from, b = (float)to;
    return (double)(z_rnd_next() * (b - a) + a);
}
inline int64_t zen_rand(int64_t from, int64_t to = 1)
{
    if (to < from) { const int64_t swap = from; from = to; to = swap; }
    return (int64_t)(z_rnd_next() * (float)(to - from + 1)) + from;
}
inline void zen_seed_rnd(int64_t seed)
{
    const int s = (int)seed & 0x7fffffff;
    z_rnd_state = s ? s : 1;
}
inline int64_t zen_rnd_seed() { return (int64_t)z_rnd_state; }

/* Trigonometry in degrees, through the same constants as the VM's dtor/rtod. */
inline double zen_asin(double value) { return std::asin(value) * 57.2957795130823208767981548141052; }
inline double zen_acos(double value) { return std::acos(value) * 57.2957795130823208767981548141052; }
inline double zen_atan(double value) { return std::atan(value) * 57.2957795130823208767981548141052; }
inline double zen_atan2(double a, double b) { return std::atan2(a, b) * 57.2957795130823208767981548141052; }

/* Date and time, formatted the way libzen's c_CurrentDate/c_CurrentTime do. */
inline std::string zen_current_date()
{
    char buffer[256];
    std::time_t now = std::time(0);
    std::strftime(buffer, sizeof(buffer), "%d %b %Y", std::localtime(&now));
    return buffer;
}
inline std::string zen_current_time()
{
    char buffer[256];
    std::time_t now = std::time(0);
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", std::localtime(&now));
    return buffer;
}

/* The process commands. CommandLine$ answers what the CLI installed as the
   script's arguments - the emitted program is started with none, so it is
   empty, which is what the VM answers for a script run without arguments. */
inline std::string zen_command_line() { return ""; }
inline std::string zen_system_property(const std::string &name)
{
    if (name == "blitzversion") return "1.108";
    if (name == "os")
    {
#if defined(_WIN32)
        return "Windows";
#elif defined(__APPLE__)
        return "MacOS";
#else
        return "Linux";
#endif
    }
    if (name == "cpu")
    {
#if defined(__x86_64__) || defined(_M_X64)
        return "x86_64";
#elif defined(__i386__) || defined(_M_IX86)
        return "x86";
#elif defined(__aarch64__)
        return "ARM64";
#elif defined(__arm__)
        return "ARM";
#else
        return "Unknown";
#endif
    }
    return "";
}
inline std::string zen_get_env(const std::string &name)
{
    const char *value = std::getenv(name.c_str());
    return value ? value : "";
}
inline void zen_set_env(const std::string &name, const std::string &value)
{
#if defined(_WIN32)
    std::string entry = name + "=" + value;
    _putenv(entry.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
}
inline void zen_debug_log(const std::string &text) { zen_stderr_line(text); }
inline void zen_exec_file(const std::string &command) { std::system(command.c_str()); }
/* Locate and Stop are accepted and do nothing, which is all the VM does with
   them (c_Locate and c_Stop are empty - a script that stops still has to be
   compiled, it just does not stop). */
inline void zen_locate(int64_t = 0, int64_t = 0) {}
inline void zen_stop() {}

/* Blocking sleep, the console path the VM takes when the host resumes Delay
   and WaitTimer by waiting for the deadline. */
inline void zen_sleep_ms(double ms)
{
    if (ms <= 0) return;
    std::this_thread::sleep_for(std::chrono::microseconds((int64_t)(ms * 1000.0)));
}
inline void zen_delay(int64_t ms) { zen_sleep_ms((double)ms); }

/* Fixed-rate timers on the millisecond clock, the same arithmetic as the VM's
   BBTimer (period 1000/hz, WaitTimer counting every period the deadline
   passed) and the same handle table (65536 and up). The VM suspends and lets
   the host resume it at the deadline; a native program has no host, so it
   sleeps to the deadline instead. */
struct ZenTimer { double period; double next; };
static ZenHandles<ZenTimer *> z_timers;
inline int64_t zen_create_timer(int64_t hertz)
{
    if (hertz <= 0) hertz = 1;
    ZenTimer *timer = new ZenTimer;
    timer->period = 1000.0 / (double)hertz;
    timer->next = (double)zen_millisecs() + timer->period;
    return z_timers.add(timer);
}
inline int64_t zen_wait_timer(int64_t timer)
{
    ZenTimer *t = z_timers.get(timer);
    if (!t) return 0;
    double now = (double)zen_millisecs();
    if (now < t->next)
    {
        zen_sleep_ms(t->next - now);
        now = (double)zen_millisecs();
    }
    const double reached = now < t->next ? t->next : now;
    int64_t ticks = 0;
    while (t->next <= reached) { t->next += t->period; ++ticks; }
    return ticks;
}
inline void zen_free_timer(int64_t timer)
{
    delete z_timers.get(timer);
    z_timers.remove(timer);
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
