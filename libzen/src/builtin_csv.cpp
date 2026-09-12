/* =========================================================
** builtin_csv.cpp — "csv" module for Zen
**
** RFC 4180-compliant CSV parser and serializer.
** No external dependencies — pure C implementation.
**
** Usage:
**   import csv;
**
**   // parse: returns array of arrays (rows × columns)
**   var rows = csv.parse("name,age\nalice,30\nbob,25");
**   print(rows[0][0]);  // name
**   print(rows[1][1]);  // 30
**
**   // parse with custom separator
**   var rows = csv.parse("a;b;c\n1;2;3", ";");
**
**   // parseMap: returns array of maps (first row = headers)
**   var maps = csv.parseMap("name,age\nalice,30");
**   print(maps[0]["name"]);  // alice
**   print(maps[0]["age"]);   // 30
**
**   // stringify: array of arrays → CSV string
**   var s = csv.stringify([["name","age"],["alice",30]]);
**   print(s);  // name,age\nalice,30
**
**   // stringify with custom separator
**   var s = csv.stringify(rows, ";");
** ========================================================= */

#include "module.h"
#include "vm.h"
#include "memory.h"
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

namespace zen
{

/* =========================================================
** Internal string buffer
** ========================================================= */

struct CsvBuf
{
    char *data;
    int   len;
    int   cap;
};

static void cb_init(CsvBuf *b)
{
    b->data = (char *)malloc(256);
    b->len  = 0;
    b->cap  = 256;
}

static void cb_free(CsvBuf *b)
{
    free(b->data);
    b->data = nullptr;
    b->len = b->cap = 0;
}

static void cb_push(CsvBuf *b, char c)
{
    if (b->len + 1 >= b->cap)
    {
        b->cap *= 2;
        b->data = (char *)realloc(b->data, b->cap);
    }
    b->data[b->len++] = c;
}

static void cb_push_str(CsvBuf *b, const char *s, int n)
{
    while (b->len + n + 1 >= b->cap)
    {
        b->cap *= 2;
        b->data = (char *)realloc(b->data, b->cap);
    }
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

/* =========================================================
** Value → string (for stringify)
** ========================================================= */

static void value_to_csv_str(Value v, CsvBuf *out)
{
    char tmp[64];
    if (is_nil(v))
    {
        /* empty cell */
    }
    else if (is_bool(v))
    {
        const char *s = v.as.boolean ? "true" : "false";
        cb_push_str(out, s, (int)strlen(s));
    }
    else if (is_int(v))
    {
        int n = snprintf(tmp, sizeof(tmp), "%lld", (long long)v.as.integer);
        cb_push_str(out, tmp, n);
    }
    else if (is_float(v))
    {
        double d = v.as.number;
        int n;
        if (d == (long long)d)
            n = snprintf(tmp, sizeof(tmp), "%.1f", d);
        else
            n = snprintf(tmp, sizeof(tmp), "%.14g", d);
        cb_push_str(out, tmp, n);
    }
    else if (is_string(v))
    {
        ObjString *os = as_string(v);
        /* Check if quoting is needed (contains comma, newline, quote) */
        bool needs_quote = false;
        for (int i = 0; i < os->length; i++)
        {
            char c = os->chars[i];
            if (c == ',' || c == '\n' || c == '\r' || c == '"')
            {
                needs_quote = true;
                break;
            }
        }
        if (needs_quote)
        {
            cb_push(out, '"');
            for (int i = 0; i < os->length; i++)
            {
                char c = os->chars[i];
                if (c == '"') cb_push(out, '"'); /* escape double-quote */
                cb_push(out, c);
            }
            cb_push(out, '"');
        }
        else
        {
            cb_push_str(out, os->chars, os->length);
        }
    }
    else
    {
        /* Other types: empty cell */
    }
}

/* =========================================================
** Parse CSV text → ObjArray of ObjArray
**
** sep: separator character (default ',')
** Returns: array of rows; each row is an array of strings.
** ========================================================= */

static ObjArray *parse_csv(VM *vm, const char *text, int text_len, char sep)
{
    GC &gc = vm->get_gc();
    ObjArray *rows = new_array(&gc);

    CsvBuf field;
    cb_init(&field);

    ObjArray *cur_row = new_array(&gc);

    /* Push a completed field (as string) into cur_row */
    auto push_field = [&]() {
        ObjString *os = vm->make_string(field.data, field.len);
        array_push(&gc, cur_row, val_obj((Obj *)os));
        field.len = 0;
    };

    /* Push cur_row into rows and start a new row */
    auto push_row = [&]() {
        /* skip completely empty trailing rows */
        if (arr_count(cur_row) == 0 && field.len == 0)
            return;
        push_field();
        array_push(&gc, rows, val_obj((Obj *)cur_row));
        cur_row = new_array(&gc);
    };

    bool in_quote = false;
    int i = 0;
    while (i < text_len)
    {
        char c = text[i];

        if (in_quote)
        {
            if (c == '"')
            {
                /* Check for escaped double-quote ("") */
                if (i + 1 < text_len && text[i + 1] == '"')
                {
                    cb_push(&field, '"');
                    i += 2;
                    continue;
                }
                in_quote = false;
            }
            else
            {
                cb_push(&field, c);
            }
        }
        else
        {
            if (c == '"')
            {
                in_quote = true;
            }
            else if (c == sep)
            {
                push_field();
            }
            else if (c == '\n')
            {
                push_row();
            }
            else if (c == '\r')
            {
                /* skip bare \r; \r\n handled by the \n on next iteration */
            }
            else
            {
                cb_push(&field, c);
            }
        }
        i++;
    }

    /* Flush last row */
    if (arr_count(cur_row) > 0 || field.len > 0)
        push_row();

    cb_free(&field);
    return rows;
}

/* =========================================================
** csv.parse(text [, sep]) → array of arrays of strings
** ========================================================= */

static int nat_csv_parse(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("csv.parse() expects (text [, sep])");
        return -1;
    }
    ObjString *text_os = as_string(args[0]);
    char sep = ',';
    if (nargs >= 2 && is_string(args[1]))
    {
        ObjString *sep_os = as_string(args[1]);
        if (sep_os->length > 0)
            sep = sep_os->chars[0];
    }

    ObjArray *rows = parse_csv(vm, text_os->chars, text_os->length, sep);
    args[0] = val_obj((Obj *)rows);
    return 1;
}

/* =========================================================
** csv.parseMap(text [, sep]) → array of maps
** First row is used as column header names.
** ========================================================= */

static int nat_csv_parse_map(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_string(args[0]))
    {
        vm->runtime_error("csv.parseMap() expects (text [, sep])");
        return -1;
    }
    ObjString *text_os = as_string(args[0]);
    char sep = ',';
    if (nargs >= 2 && is_string(args[1]))
    {
        ObjString *sep_os = as_string(args[1]);
        if (sep_os->length > 0)
            sep = sep_os->chars[0];
    }

    ObjArray *rows = parse_csv(vm, text_os->chars, text_os->length, sep);
    GC &gc = vm->get_gc();
    int nrows = arr_count(rows);

    if (nrows == 0)
    {
        /* No rows → return empty array */
        ObjArray *empty = new_array(&gc);
        args[0] = val_obj((Obj *)empty);
        return 1;
    }

    /* First row = headers */
    Value header_val = rows->data[0];
    if (!is_array(header_val))
    {
        vm->runtime_error("csv.parseMap(): invalid header row");
        return -1;
    }
    ObjArray *headers = as_array(header_val);
    int ncols = arr_count(headers);

    ObjArray *result = new_array(&gc);

    for (int r = 1; r < nrows; r++)
    {
        Value row_val = rows->data[r];
        if (!is_array(row_val)) continue;
        ObjArray *row = as_array(row_val);

        ObjMap *map = new_map(&gc);
        for (int c = 0; c < ncols; c++)
        {
            Value key = (c < arr_count(headers)) ? headers->data[c] : val_nil();
            Value val = (c < arr_count(row))    ? row->data[c]     : val_nil();
            if (is_string(key))
                map_set(&gc, map, key, val);
        }
        array_push(&gc, result, val_obj((Obj *)map));
    }

    args[0] = val_obj((Obj *)result);
    return 1;
}

/* =========================================================
** csv.stringify(rows [, sep]) → string
** rows: array of arrays. Values are coerced to strings.
** ========================================================= */

static int nat_csv_stringify(VM *vm, Value *args, int nargs)
{
    if (nargs < 1 || !is_array(args[0]))
    {
        vm->runtime_error("csv.stringify() expects (rows [, sep])");
        return -1;
    }
    ObjArray *rows = as_array(args[0]);
    char sep = ',';
    if (nargs >= 2 && is_string(args[1]))
    {
        ObjString *sep_os = as_string(args[1]);
        if (sep_os->length > 0)
            sep = sep_os->chars[0];
    }

    CsvBuf out;
    cb_init(&out);

    int nrows = arr_count(rows);
    for (int r = 0; r < nrows; r++)
    {
        Value row_val = rows->data[r];
        if (!is_array(row_val))
        {
            /* Single value row */
            value_to_csv_str(row_val, &out);
            cb_push(&out, '\n');
            continue;
        }
        ObjArray *row = as_array(row_val);
        int ncols = arr_count(row);
        for (int c = 0; c < ncols; c++)
        {
            if (c > 0) cb_push(&out, sep);
            /* Check if cell needs quoting with custom sep */
            if (sep != ',')
            {
                /* Temporarily: quoting logic inside value_to_csv_str only checks
                   for ',' — for custom sep, we need a slightly different approach.
                   Simple fix: use value_to_csv_str (it only quotes for comma/newline/quote)
                   then re-quote if sep appears in the output. For simplicity we
                   handle this by converting to a temp buffer and checking. */
                CsvBuf cell;
                cb_init(&cell);
                value_to_csv_str(row->data[c], &cell);

                /* Check if sep or newline appears */
                bool needs_q = false;
                for (int k = 0; k < cell.len; k++)
                {
                    if (cell.data[k] == sep || cell.data[k] == '\n' ||
                        cell.data[k] == '\r' || cell.data[k] == '"')
                    { needs_q = true; break; }
                }
                if (needs_q)
                {
                    cb_push(&out, '"');
                    for (int k = 0; k < cell.len; k++)
                    {
                        if (cell.data[k] == '"') cb_push(&out, '"');
                        cb_push(&out, cell.data[k]);
                    }
                    cb_push(&out, '"');
                }
                else
                {
                    cb_push_str(&out, cell.data, cell.len);
                }
                cb_free(&cell);
            }
            else
            {
                value_to_csv_str(row->data[c], &out);
            }
        }
        cb_push(&out, '\n');
    }

    ObjString *result = vm->make_string(out.data, out.len);
    cb_free(&out);
    args[0] = val_obj((Obj *)result);
    return 1;
}

/* =========================================================
** Registration
** ========================================================= */

static const NativeReg csv_functions[] = {
    {"parse",     nat_csv_parse,     -1},
    {"parseMap",  nat_csv_parse_map, -1},
    {"stringify", nat_csv_stringify, -1},
};

extern const NativeLib zen_lib_csv = {
    "csv",
    csv_functions,
    3,
    nullptr, /* no constants */
    0,
    nullptr  /* no init_fn */
};

} /* namespace zen */
