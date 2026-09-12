# userlibs

Blitz3D-style user libraries: a `.decls` file declares functions that live
in a shared library, and the compiler then accepts calls to them as if they
were built-in commands.

Both `zenblitz` and `zenblitz-rt` read this directory when it sits next to
them, so a program built with `--build` finds the same commands as long as
`userlibs/` travels with it.

## A .decls file

```
; mylib.decls
.lib "libmy.so"

AddInts%(a%, b%) : "add_ints"
Scale#(x#, k#)   : "scale"
Greet$(name$)    : "greet"
DoThing(n%)      : "do_thing"
```

- `.lib` names the shared library. A bare name is looked up next to the
  `.decls` file, which is where the library is normally kept; give a path to
  load one from elsewhere. Use the name for the platform you are on
  (`libmy.so` on Linux, `my.dll` on Windows, `libmy.dylib` on macOS), or ship
  one `.decls` per platform.
- The tag after the function name is its return type — `%` int, `#` float,
  `$` string, nothing for no result. Parameters take the same tags.
- The quoted name after `:` is the symbol exported by the library; without
  it the Blitz name is used as the symbol.
- `;` or `#` start a comment line.

## The C side

Parameters arrive as `long long` (`%`), `double` (`#`) and `const char *`
(`$`); a `$` result must be a pointer that stays valid until the next call
into the library. Export with C linkage:

```c
/* built with: gcc -shared -fPIC -o libmy.so mylib.c */
int add_ints(long long a, long long b) { return (int)(a + b); }
double scale(double x, double k) { return x * k; }
const char *greet(const char *name) {
    static char buf[256];
    snprintf(buf, sizeof(buf), "hello, %s!", name);
    return buf;
}
```

At most 8 parameters per function.

## Limits

Userlibs need dynamic library loading, so they work on desktop platforms
(Linux, Windows, macOS) and on x86-64 or AArch64. On web and Android builds
there is nothing to load a library from: declared functions still compile,
but calling one reports that it is unavailable.

A program that calls a userlib needs that userlib installed wherever it
runs. Running one without it fails at the first call with a message naming
the missing command, rather than a generic error.
