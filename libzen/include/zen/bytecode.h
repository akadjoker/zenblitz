#ifndef ZEN_BYTECODE_H
#define ZEN_BYTECODE_H

#include "common.h"
#include <cstddef>
#include <cstdint>

namespace zen
{

    static constexpr uint8_t ZEN_BYTECODE_MAGIC[5] = {'Z', 'E', 'N', 'B', 'C'};
    /* major 3: OP_HALT's numeric value changed (opcodes.h) — it now sits
    **          permanently last in the OpCode enum instead of getting
    **          reshuffled by each new opcode appended "before OP_HALT".
    **          Every file at major < 3 has a trailing HALT that would
    **          silently decode as a different (and possibly wider)
    **          instruction under the new numbering — a real corruption, not
    **          just a version mismatch, so this must be a MAJOR bump: the
    **          loader's `major != ZEN_BYTECODE_VERSION_MAJOR` check rejects
    **          those files outright instead of misreading them. */
    static constexpr uint16_t ZEN_BYTECODE_VERSION_MAJOR = 3;
    /* minor 2: ObjFunc gained generic_arity (reified generics, f<T>(...)) —
    **          written at the end of write_func(); read_func() defaults it to
    **          0 for minor < 2. See read_func()/write_func() in bytecode.cpp. */
    static constexpr uint16_t ZEN_BYTECODE_VERSION_MINOR = 2;

    struct BytecodeStats
    {
        uint32_t functions = 0;
        uint32_t processes = 0;
        uint32_t classes = 0;
        uint32_t closures = 0;
        uint32_t strings = 0;
        uint32_t constants = 0;
        uint32_t instructions = 0;
        uint32_t globals = 0;
        uint32_t selectors = 0;
        size_t bytes = 0;
    };

    bool is_bytecode_buffer(const uint8_t *data, size_t size);

    bool dump_bytecode_file(ObjFunc *func, const char *path, bool strip_debug = false,
                            char *err = nullptr, int err_len = 0);

    bool dump_bytecode_file(VM *vm, ObjFunc *func, const char *path, bool strip_debug = false,
                            char *err = nullptr, int err_len = 0);

    bool dump_bytecode_file(VM *vm, ObjFunc *func, const char *path, bool strip_debug,
                            BytecodeStats *stats, char *err = nullptr, int err_len = 0);

    ObjFunc *load_bytecode_buffer(VM *vm, const uint8_t *data, size_t size,
                                  char *err = nullptr, int err_len = 0);

} /* namespace zen */

#endif /* ZEN_BYTECODE_H */
