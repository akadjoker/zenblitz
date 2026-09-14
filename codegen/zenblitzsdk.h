#ifndef ZENBLITZSDK_H
#define ZENBLITZSDK_H

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <SDL2/SDL_rwops.h>

/* Filesystem ops (stat, mkdir, opendir) are not in SDL2, but
** Emscripten implements POSIX through its virtual FS, and desktop
** has them natively, so these work on every target. */
#if defined(_WIN32)
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* ================= File handles via SDL_RWops =================
** Integer handles (1-based, 0 = invalid), matching the VM.
** All file I/O goes through SDL_RWops so it works on desktop,
** Web (Emscripten virtual FS) and mobile. */

struct ZenFileEntry { SDL_RWops *rw; };

static std::vector<ZenFileEntry> z_file_table;

static inline int64_t z_file_add(SDL_RWops *rw)
{
    if (!rw) return 0;
    for (size_t i = 0; i < z_file_table.size(); ++i)
        if (!z_file_table[i].rw) { z_file_table[i].rw = rw; return (int64_t)(i + 1); }
    z_file_table.push_back(ZenFileEntry{rw});
    return (int64_t)z_file_table.size();
}

static inline SDL_RWops *z_file_get(int64_t h)
{
    if (h <= 0 || (size_t)h > z_file_table.size()) return 0;
    return z_file_table[(size_t)h - 1].rw;
}

static inline void z_file_remove(int64_t h)
{
    if (h <= 0 || (size_t)h > z_file_table.size()) return;
    z_file_table[(size_t)h - 1].rw = 0;
}

static inline int64_t z_file_size(SDL_RWops *rw)
{
    if (!rw) return 0;
    Sint64 sz = SDL_RWsize(rw);
    if (sz < 0)
    {
        Sint64 cur = SDL_RWtell(rw);
        sz = SDL_RWseek(rw, 0, RW_SEEK_END);
        SDL_RWseek(rw, cur, RW_SEEK_SET);
    }
    return sz < 0 ? 0 : (int64_t)sz;
}

inline int64_t zen_WriteFile(const std::string &filename)
{
    return z_file_add(SDL_RWFromFile(filename.c_str(), "w+b"));
}
inline int64_t zen_ReadFile(const std::string &filename)
{
    return z_file_add(SDL_RWFromFile(filename.c_str(), "rb"));
}
inline int64_t zen_OpenFile(const std::string &filename)
{
    return z_file_add(SDL_RWFromFile(filename.c_str(), "r+b"));
}
inline void zen_CloseFile(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    if (rw) { SDL_RWclose(rw); z_file_remove(h); }
}
inline int64_t zen_FilePos(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    return rw ? (int64_t)SDL_RWtell(rw) : 0;
}
inline int64_t zen_SeekFile(int64_t h, int64_t pos)
{
    SDL_RWops *rw = z_file_get(h);
    if (!rw) return 0;
    Sint64 r = SDL_RWseek(rw, (Sint64)pos, RW_SEEK_SET);
    return r < 0 ? 0 : (int64_t)r;
}
inline int64_t zen_Eof(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    if (!rw) return -1;
    return SDL_RWtell(rw) >= z_file_size(rw) ? 1 : 0;
}

inline int64_t zen_ReadByte(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    unsigned char b = 0;
    if (rw) SDL_RWread(rw, &b, 1, 1);
    return (int64_t)b;
}
inline int64_t zen_ReadShort(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    unsigned short s = 0;
    if (rw) SDL_RWread(rw, &s, 2, 1);
    return (int64_t)s;
}
inline int64_t zen_ReadInt(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    int i = 0;
    if (rw) SDL_RWread(rw, &i, 4, 1);
    return (int64_t)i;
}
inline double zen_ReadFloat(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    float fl = 0;
    if (rw) SDL_RWread(rw, &fl, 4, 1);
    return (double)fl;
}
inline std::string zen_ReadString(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    int n = 0;
    if (!rw || SDL_RWread(rw, &n, 4, 1) != 1 || n < 0) return "";
    std::string buf((size_t)n, '\0');
    if (n > 0) SDL_RWread(rw, &buf[0], (size_t)n, 1);
    return buf;
}
inline std::string zen_ReadLine(int64_t h)
{
    SDL_RWops *rw = z_file_get(h);
    std::string s;
    if (rw)
    {
        char c;
        while (SDL_RWread(rw, &c, 1, 1) == 1 && c != '\n') s += c;
        if (!s.empty() && s[s.size() - 1] == '\r') s.resize(s.size() - 1);
    }
    return s;
}

inline void zen_WriteByte(int64_t h, int64_t v)
{
    SDL_RWops *rw = z_file_get(h);
    if (rw) { unsigned char b = (unsigned char)v; SDL_RWwrite(rw, &b, 1, 1); }
}
inline void zen_WriteShort(int64_t h, int64_t v)
{
    SDL_RWops *rw = z_file_get(h);
    if (rw) { unsigned short s = (unsigned short)v; SDL_RWwrite(rw, &s, 2, 1); }
}
inline void zen_WriteInt(int64_t h, int64_t v)
{
    SDL_RWops *rw = z_file_get(h);
    if (rw) { int i = (int)v; SDL_RWwrite(rw, &i, 4, 1); }
}
inline void zen_WriteFloat(int64_t h, double v)
{
    SDL_RWops *rw = z_file_get(h);
    if (rw) { float fl = (float)v; SDL_RWwrite(rw, &fl, 4, 1); }
}
inline void zen_WriteString(int64_t h, const std::string &s)
{
    SDL_RWops *rw = z_file_get(h);
    if (!rw) return;
    int n = (int)s.size();
    SDL_RWwrite(rw, &n, 4, 1);
    if (n > 0) SDL_RWwrite(rw, s.data(), (size_t)n, 1);
}
inline void zen_WriteLine(int64_t h, const std::string &s)
{
    SDL_RWops *rw = z_file_get(h);
    if (!rw) return;
    SDL_RWwrite(rw, s.data(), s.size(), 1);
    SDL_RWwrite(rw, "\n", 1, 1);
}

/* ================= Filesystem =================
** SDL2 has no stat/mkdir/opendir API, but Emscripten implements
** POSIX through its virtual FS and desktop has them natively. */

inline int64_t zen_FileSize(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return -1;
    return (int64_t)st.st_size;
}
inline int64_t zen_FileType(const std::string &path)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return 0;
    return S_ISDIR(st.st_mode) ? 2 : 1;
}
inline void zen_CopyFile(const std::string &src, const std::string &dst)
{
    SDL_RWops *in = SDL_RWFromFile(src.c_str(), "rb");
    if (!in) return;
    SDL_RWops *out = SDL_RWFromFile(dst.c_str(), "wb");
    if (!out) { SDL_RWclose(in); return; }
    char buf[4096];
    size_t n;
    while ((n = SDL_RWread(in, buf, 1, sizeof(buf))) > 0) SDL_RWwrite(out, buf, 1, n);
    SDL_RWclose(in); SDL_RWclose(out);
}
inline void zen_DeleteFile(const std::string &path)
{
    remove(path.c_str());
}

/* ================= Directories ================= */

struct ZenDirEntry
{
#if defined(_WIN32)
    HANDLE h;
    WIN32_FIND_DATAA fd;
    bool pending;
#else
    DIR *dp;
#endif
};

static std::vector<ZenDirEntry*> z_dir_table;

static inline int64_t z_dir_add(ZenDirEntry *d)
{
    if (!d) return 0;
    for (size_t i = 0; i < z_dir_table.size(); ++i)
        if (!z_dir_table[i]) { z_dir_table[i] = d; return (int64_t)(i + 1); }
    z_dir_table.push_back(d);
    return (int64_t)z_dir_table.size();
}

static inline ZenDirEntry *z_dir_get(int64_t h)
{
    if (h <= 0 || (size_t)h > z_dir_table.size()) return 0;
    return z_dir_table[(size_t)h - 1];
}

inline int64_t zen_ReadDir(const std::string &path)
{
#if defined(_WIN32)
    char pattern[4096];
    snprintf(pattern, sizeof(pattern), "%s\\*", path.c_str());
    ZenDirEntry *d = new ZenDirEntry;
    d->h = FindFirstFileA(pattern, &d->fd);
    if (d->h == INVALID_HANDLE_VALUE) { delete d; return 0; }
    d->pending = true;
    return z_dir_add(d);
#else
    ZenDirEntry *d = new ZenDirEntry;
    d->dp = opendir(path.c_str());
    if (!d->dp) { delete d; return 0; }
    return z_dir_add(d);
#endif
}

inline std::string zen_NextFile(int64_t h)
{
    ZenDirEntry *d = z_dir_get(h);
    if (!d) return "";
#if defined(_WIN32)
    if (d->pending) { d->pending = false; return d->fd.cFileName; }
    return FindNextFileA(d->h, &d->fd) ? std::string(d->fd.cFileName) : "";
#else
    struct dirent *e = readdir(d->dp);
    return e ? std::string(e->d_name) : "";
#endif
}

inline void zen_CloseDir(int64_t h)
{
    ZenDirEntry *d = z_dir_get(h);
    if (!d) return;
#if defined(_WIN32)
    if (d->h != INVALID_HANDLE_VALUE) FindClose(d->h);
#else
    if (d->dp) closedir(d->dp);
#endif
    delete d;
    z_dir_table[(size_t)h - 1] = 0;
}

inline void zen_CreateDir(const std::string &path)
{
#if defined(_WIN32)
    _mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0777);
#endif
}
inline void zen_DeleteDir(const std::string &path)
{
#if defined(_WIN32)
    _rmdir(path.c_str());
#else
    rmdir(path.c_str());
#endif
}
inline void zen_ChangeDir(const std::string &path)
{
#if defined(_WIN32)
    _chdir(path.c_str());
#else
    chdir(path.c_str());
#endif
}
inline std::string zen_CurrentDir()
{
    char buf[4096];
#if defined(_WIN32)
    return _getcwd(buf, sizeof(buf)) ? std::string(buf) : "";
#else
    return getcwd(buf, sizeof(buf)) ? std::string(buf) : "";
#endif
}

/* ================= Banks =================
** Byte buffers with integer handles (1-based, 0 = invalid). */

static std::vector<std::vector<uint8_t>*> z_bank_table;

static inline std::vector<uint8_t> *z_bank_get(int64_t h)
{
    if (h <= 0 || (size_t)h > z_bank_table.size()) return 0;
    return z_bank_table[(size_t)h - 1];
}

inline int64_t zen_CreateBank(int64_t size = 0)
{
    auto *b = new std::vector<uint8_t>((size_t)(size < 0 ? 0 : size), 0);
    for (size_t i = 0; i < z_bank_table.size(); ++i)
        if (!z_bank_table[i]) { z_bank_table[i] = b; return (int64_t)(i + 1); }
    z_bank_table.push_back(b);
    return (int64_t)z_bank_table.size();
}
inline void zen_FreeBank(int64_t h)
{
    if (h <= 0 || (size_t)h > z_bank_table.size()) return;
    delete z_bank_table[(size_t)h - 1];
    z_bank_table[(size_t)h - 1] = 0;
}
inline int64_t zen_BankSize(int64_t h)
{
    std::vector<uint8_t> *b = z_bank_get(h);
    return b ? (int64_t)b->size() : 0;
}
inline void zen_ResizeBank(int64_t h, int64_t size)
{
    std::vector<uint8_t> *b = z_bank_get(h);
    if (!b || size < 0) return;
    b->resize((size_t)size, 0);
}
inline void zen_CopyBank(int64_t src, int64_t src_off, int64_t dst, int64_t dst_off, int64_t count)
{
    std::vector<uint8_t> *s = z_bank_get(src);
    std::vector<uint8_t> *d = z_bank_get(dst);
    if (!s || !d) { std::fflush(stdout); std::exit(1); }
    if (src_off < 0 || dst_off < 0 || count < 0 ||
        (size_t)(src_off + count) > s->size() || (size_t)(dst_off + count) > d->size())
    { std::fflush(stdout); std::exit(1); }
    memmove(d->data() + dst_off, s->data() + src_off, (size_t)count);
}

static inline void z_bank_check(int64_t h, int64_t off, int64_t size)
{
    std::vector<uint8_t> *b = z_bank_get(h);
    if (!b) { std::fflush(stdout); std::exit(1); }
    if (off < 0 || off + size > (int64_t)b->size()) { std::fflush(stdout); std::exit(1); }
}

inline int64_t zen_PeekByte(int64_t h, int64_t off)
{
    z_bank_check(h, off, 1);
    return (int64_t)(*z_bank_get(h))[(size_t)off];
}
inline int64_t zen_PeekShort(int64_t h, int64_t off)
{
    z_bank_check(h, off, 2);
    unsigned short s;
    memcpy(&s, z_bank_get(h)->data() + off, 2);
    return (int64_t)s;
}
inline int64_t zen_PeekInt(int64_t h, int64_t off)
{
    z_bank_check(h, off, 4);
    int i;
    memcpy(&i, z_bank_get(h)->data() + off, 4);
    return (int64_t)i;
}
inline double zen_PeekFloat(int64_t h, int64_t off)
{
    z_bank_check(h, off, 4);
    float f;
    memcpy(&f, z_bank_get(h)->data() + off, 4);
    return (double)f;
}
inline void zen_PokeByte(int64_t h, int64_t off, int64_t v)
{
    z_bank_check(h, off, 1);
    (*z_bank_get(h))[(size_t)off] = (uint8_t)v;
}
inline void zen_PokeShort(int64_t h, int64_t off, int64_t v)
{
    z_bank_check(h, off, 2);
    unsigned short s = (unsigned short)v;
    memcpy(z_bank_get(h)->data() + off, &s, 2);
}
inline void zen_PokeInt(int64_t h, int64_t off, int64_t v)
{
    z_bank_check(h, off, 4);
    int i = (int)v;
    memcpy(z_bank_get(h)->data() + off, &i, 4);
}
inline void zen_PokeFloat(int64_t h, int64_t off, double v)
{
    z_bank_check(h, off, 4);
    float f = (float)v;
    memcpy(z_bank_get(h)->data() + off, &f, 4);
}

inline int64_t zen_ReadBytes(int64_t bank, int64_t file, int64_t offset, int64_t count)
{
    z_bank_check(bank, offset, count < 0 ? 0 : count);
    SDL_RWops *rw = z_file_get(file);
    if (!rw || count < 0) return 0;
    return (int64_t)SDL_RWread(rw, z_bank_get(bank)->data() + offset, 1, (size_t)count);
}
inline int64_t zen_WriteBytes(int64_t bank, int64_t file, int64_t offset, int64_t count)
{
    z_bank_check(bank, offset, count < 0 ? 0 : count);
    SDL_RWops *rw = z_file_get(file);
    if (!rw || count < 0) return 0;
    return (int64_t)SDL_RWwrite(rw, z_bank_get(bank)->data() + offset, 1, (size_t)count);
}

#endif
