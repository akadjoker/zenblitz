#ifndef ZEN_NAME_TABLES_H
#define ZEN_NAME_TABLES_H

#include "vm.h"
#include <cstring>

namespace zen
{
    struct NameEntry
    {
        const char *name;
        uint8_t len;
        int id;
    };

    static inline int lookup_name(const NameEntry *entries, int count, const char *name, int len)
    {
        for (int i = 0; i < count; i++)
        {
            if (entries[i].len == len &&
                memcmp(entries[i].name, name, (size_t)len) == 0)
                return entries[i].id;
        }
        return -1;
    }

    template <size_t N>
    static inline int lookup_name(const NameEntry (&entries)[N], const char *name, int len)
    {
        return lookup_name(entries, (int)N, name, len);
    }

    static const NameEntry kOperatorNames[] = {
        {"__add__", 7, VM::SLOT_ADD},
        {"__radd__", 8, VM::SLOT_RADD},
        {"__sub__", 7, VM::SLOT_SUB},
        {"__rsub__", 8, VM::SLOT_RSUB},
        {"__mul__", 7, VM::SLOT_MUL},
        {"__rmul__", 8, VM::SLOT_RMUL},
        {"__div__", 7, VM::SLOT_DIV},
        {"__rdiv__", 8, VM::SLOT_RDIV},
        {"__mod__", 7, VM::SLOT_MOD},
        {"__rmod__", 8, VM::SLOT_RMOD},
        {"__neg__", 7, VM::SLOT_NEG},
        {"__eq__", 6, VM::SLOT_EQ},
        {"__lt__", 6, VM::SLOT_LT},
        {"__le__", 6, VM::SLOT_LE},
        {"__str__", 7, VM::SLOT_STR},
    };

    static inline int operator_slot_for_name(const char *name, int len)
    {
        return lookup_name(kOperatorNames, name, len);
    }

    enum ArrayMethodId
    {
        ARRAY_PUSH,
        ARRAY_POP,
        ARRAY_LEN,
        ARRAY_REMOVE,
        ARRAY_INSERT,
        ARRAY_SLICE,
        ARRAY_REVERSE,
        ARRAY_CLEAR,
        ARRAY_CONTAINS,
        ARRAY_JOIN,
        ARRAY_SORT,
        ARRAY_INDEX_OF,
        ARRAY_DUMP,
    };

    static const NameEntry kArrayMethods[] = {
        {"push", 4, ARRAY_PUSH},
        {"pop", 3, ARRAY_POP},
        {"len", 3, ARRAY_LEN},
        {"remove", 6, ARRAY_REMOVE},
        {"insert", 6, ARRAY_INSERT},
        {"slice", 5, ARRAY_SLICE},
        {"reverse", 7, ARRAY_REVERSE},
        {"clear", 5, ARRAY_CLEAR},
        {"contains", 8, ARRAY_CONTAINS},
        {"join", 4, ARRAY_JOIN},
        {"sort", 4, ARRAY_SORT},
        {"index_of", 8, ARRAY_INDEX_OF},
        {"dump", 4, ARRAY_DUMP},
    };

    enum BufferMethodId
    {
        BUFFER_LEN,
        BUFFER_FILL,
        BUFFER_BYTE_LEN,
    };

    static const NameEntry kBufferMethods[] = {
        {"len", 3, BUFFER_LEN},
        {"fill", 4, BUFFER_FILL},
        {"byte_len", 8, BUFFER_BYTE_LEN},
    };

    enum MapMethodId
    {
        MAP_SET,
        MAP_GET,
        MAP_HAS,
        MAP_DELETE,
        MAP_KEYS,
        MAP_VALUES,
        MAP_SIZE,
        MAP_CLEAR,
        MAP_DUMP,
    };

    static const NameEntry kMapMethods[] = {
        {"set", 3, MAP_SET},
        {"get", 3, MAP_GET},
        {"has", 3, MAP_HAS},
        {"delete", 6, MAP_DELETE},
        {"keys", 4, MAP_KEYS},
        {"values", 6, MAP_VALUES},
        {"size", 4, MAP_SIZE},
        {"clear", 5, MAP_CLEAR},
        {"dump", 4, MAP_DUMP},
    };

    enum SetMethodId
    {
        SET_ADD,
        SET_HAS,
        SET_DELETE,
        SET_SIZE,
        SET_CLEAR,
        SET_VALUES,
        SET_DUMP,
    };

    static const NameEntry kSetMethods[] = {
        {"add", 3, SET_ADD},
        {"has", 3, SET_HAS},
        {"delete", 6, SET_DELETE},
        {"size", 4, SET_SIZE},
        {"clear", 5, SET_CLEAR},
        {"values", 6, SET_VALUES},
        {"dump", 4, SET_DUMP},
    };

    enum StringMethodId
    {
        STRING_LEN,
        STRING_SUB,
        STRING_FIND,
        STRING_UPPER,
        STRING_LOWER,
        STRING_SPLIT,
        STRING_TRIM,
        STRING_REPLACE,
        STRING_STARTS_WITH,
        STRING_ENDS_WITH,
        STRING_CHAR_AT,
        STRING_BYTE_AT,
        STRING_REPEAT,
        STRING_COUNT,
        STRING_PAD_LEFT,
        STRING_PAD_RIGHT,
        STRING_CONTAINS,
        STRING_REVERSE,
    };

    static const NameEntry kStringMethods[] = {
        {"len", 3, STRING_LEN},
        {"sub", 3, STRING_SUB},
        {"find", 4, STRING_FIND},
        {"upper", 5, STRING_UPPER},
        {"lower", 5, STRING_LOWER},
        {"split", 5, STRING_SPLIT},
        {"trim", 4, STRING_TRIM},
        {"replace", 7, STRING_REPLACE},
        {"starts_with", 11, STRING_STARTS_WITH},
        {"ends_with", 9, STRING_ENDS_WITH},
        {"char_at", 7, STRING_CHAR_AT},
        {"byte_at", 7, STRING_BYTE_AT},
        {"repeat", 6, STRING_REPEAT},
        {"count", 5, STRING_COUNT},
        {"pad_left", 8, STRING_PAD_LEFT},
        {"pad_right", 9, STRING_PAD_RIGHT},
        {"contains", 8, STRING_CONTAINS},
        {"reverse", 7, STRING_REVERSE},
    };
}

#endif /* ZEN_NAME_TABLES_H */
