# Plano: ZenVM com linguagem Blitz Basic

Objectivo: um projecto novo, separado do zenvm, em que a linguagem de entrada é
Blitz Basic (sintaxe do Blitz3D) e o motor de execução é a VM do Zen. O zenvm
não é alterado; o projecto novo leva uma cópia do `libzen/` e substitui o
compilador (lexer/parser/codegen) por um compilador Blitz.

## 0. Arranque do projecto

- Criar `blitzvm/` (nome a confirmar) com: `libzen/` copiado do zenvm, `bb/`
  (o compilador Blitz), `runtime/` (comandos Blitz), `cli/`, `tests/`.
- Apagar do `libzen/` copiado o que é só da sintaxe Zen: `lexer.cpp`,
  `compiler.cpp`, `compiler_expressions.cpp`, `compiler_statements.cpp`,
  `compiler.h`, `lexer.h`. Fica: `vm.cpp`, `vm_dispatch.cpp`, `memory.cpp`,
  `emitter.cpp`, `bytecode.cpp`, `debug.cpp`, `object.h`, `value.h`,
  `opcodes.h`, e os `builtin_*.cpp` que interessam (file, fs, os, time, math).
- CMake: alvo `libzen` (VM) + alvo `bb` (executável). Sem módulos gráficos
  nesta fase.

## 1. Lexer Blitz (toker)

Fonte: `blitz3d/compiler/toker.cpp` (zlib). Regras a respeitar:
- Case-insensitive; keywords de duas palavras (`End If`, `Else If`,
  `End Function`, `End Type`, `End Select`).
- Sufixos de tipo `%` `#` `$` e `.Tipo`; `$FF` hex, `%101` binário.
- `;` comentário até fim de linha; `:` e newline separam statements (o
  newline é token, ao contrário do Zen).
- Sem escapes em strings (`"` até `"`).

## 2. Parser e análise semântica (tipagem estática)

Fonte: `parser.cpp`, `*node.cpp` (parte `semant`) do Blitz3D. Reaproveitar tal
como está, sem Win32: dá gratuitamente a semântica exacta do Blitz.
- Declaração implícita de locais; `Global`/`Local`/`Const`; `Dim`; `Type`/`Field`.
- Conversões automáticas int/float/string; constant folding.
- Funções com parâmetros por defeito; `Return` tipado pelo sufixo.
- `Include` resolvido em relação ao ficheiro que inclui.
- `Data` só no programa principal; `Gosub` só no programa principal.

## 3. Gerador de bytecode Zen (a parte nova)

Substitui o `translate` para x86 por um `emit` que usa o `Emitter` do Zen.

| Blitz | Zen |
|---|---|
| int / float / string | VAL_INT / VAL_FLOAT / ObjString |
| Null / objecto | nil / ObjInstance |
| Locais e parâmetros | registos (params em R0..) |
| Globais | globais `_v<nome>` (GETGLOBAL/SETGLOBAL) |
| Funções | `_f<nome>`, OP_CLOSURE no arranque, OP_CALLGLOBAL |
| Comandos nativos | `_f<nome>` como natives (mesmo caminho de chamada) |
| `Type` | classe `_t<Nome>` com campos do user + `__prev`, `__next`, `__alive`, `__handle`; lista por tipo em `_t<Nome>_first/_last` |
| `New` / `Delete` / `Insert` / `After` / `Before` / `Delete Each` | natives `__bb*` (desligam da lista, marcam `__alive=false`; iteração continua a funcionar) |
| `First` / `Last` | GETGLOBAL directo |
| `For Each` | inline: `__next` + salto de objectos apagados |
| `obj = Null` | nil OU `__alive = false` (semântica Blitz) |
| `Dim a(x,y)` | array Zen achatado em `_a<nome>` + tamanhos `_a<nome>_s<k>` |
| `Local a[10]` / `Field a[10]` | array Zen por instância |
| `Data` / `Read` / `Restore` | array `__DATA` construído em compile-time + `__DATAPTR` |
| `Goto` | JMP com labels resolvidas no fim da função |
| `Gosub` / `Return` | pilha de ids num registo + dispatcher no fim do main |
| `Select` | temp + EQJMPIFNOT por `Case` |
| `If a < b` | saltos fundidos (LT/LE/EQ/NE JMPIFNOT); strings via OP_LT + JMPIFNOT |
| `x = (a > b)` | valor 1/0 (Blitz não tem bool) |
| `And`/`Or`/`Xor` | bitwise (BAND/BOR/BXOR); short-circuit só quando o lado direito é puro |
| `/` int, `Mod` | OP_IDIV, OP_MOD; float: OP_DIV, `^` OP_POW |
| `Shr` | lógico sobre 32 bits (máscara + SHR); `Sar` = OP_SHR |
| float→int | native `__bbFtoI` (arredonda como o Blitz) |
| float→string | native `__bbFtoStr` (formato do Blitz: `1.5`, `10.0`, `0.333333`) |
| `Sqr`, `Sin`, `Cos`, ... | opcodes da VM inline (RAD/DEG para graus) |
| `End` | OP_HALT inline (funciona dentro de funções) |
| `Handle` / `Object.T` | natives com tabela de handles |

## 4. Alterações à VM (só na cópia do projecto novo)

1. `OP_CONCAT`: usar `new_string_concat` em vez de `string_append_inplace`.
   Strings com mais de 128 chars são reutilizadas "in place" e isso quebra a
   semântica de valor (`b$ = a$ : a$ = a$ + "x"` altera `b$`).
2. Opcional (perf): `OP_TOINT` (round) e `OP_TOFLOAT` para evitar natives nas
   conversões; `OP_TOSTRING` já serve para int.
3. Opcional (fidelidade): modo `--int32` com wrap a 32 bits nas operações
   inteiras (LCGs e hashes de programas Blitz antigos dependem disso).
4. Processos, fibers e classes ficam na VM: podem servir mais tarde para
   extensões (por exemplo `Process` estilo DIV dentro do Blitz).

## 5. Runtime de comandos

Usar as strings de assinatura do Blitz original (`"$Left$string%count"`,
`"%Rand%from%to=1"`, ...) para registar os natives e construir o `Environ` do
compilador: mesmos nomes, mesmos defaults, mesma ordem.
- Fase A (consola): Print, Write, Input, strings, matemática, Rnd/Rand (mesmo
  LCG), MilliSecs, Delay, CommandLine, RuntimeError, timers.
- Fase B: ficheiros/streams, directorias, banks (Peek/Poke), CopyFile, etc.
- Fase C (fora deste plano): Graphics/2D, input, som, 3D sobre o razor3d.

## 6. Ferramentas e testes

- `bb ficheiro.bb`, `bb --dis` (disassembler do Zen), `bb --debug` (bounds
  checks por dimensão nos arrays).
- Testes snapshot `tests/*.bb` + `expected/*.out` (mesmo estilo do zenvm).
- Benchmarks contra o `zen` equivalente: `fib(30)`, loops int/float, `For Each`.

## 7. Diferenças conhecidas em relação ao Blitz3D

- Inteiros de 64 bits e floats de 64 bits (sem overflow a 32 bits por defeito).
- `1e10` imprime `1e+10` em vez de `1e+010`.
- Divisão inteira por zero devolve 0 em vez de erro.
- Sem debugger/IDE; erros de runtime mostram ficheiro e linha.
- Profundidade de recursão limitada pelos frames da VM (512 por defeito).

## 8. Estado actual (11 Set 2026)

Feito, dentro do zenvm: o compilador Zen antigo foi removido e substituído
pelo compilador Blitz (`libzen/src/bb_*.cpp`, `compiler.cpp`), mantendo a VM
intacta. Fases 1, 2, 3 e 5-A completas; `tests/*.bb` com snapshots a passar;
performance ao nível do compilador Zen anterior. Os testes `.zen` antigos
estão em `tests/legacy_zen/`.

## 9. Redução do runtime (12 Set 2026)

Feito no zenblitz: fibers, processos, classes, closures, upvalues, generics,
maps, sets, slices, `import` e os builtins da linguagem Zen foram removidos.
Ficou: dispatch, GC, strings, arrays, buffers, structs (Types), bytecode.
`OP_CONCAT` já não faz append in-place (semântica de valor). Os Types são
auto-descritivos no bytecode (os nomes dos campos levam o tipo) e as globais
com valor (Data, strings) são serializadas, por isso `--dump` / load funciona
sem o compilador. A VM pode suspender numa nativa (`request_suspend`) para o
loop da web. Core: 28 000 → 10 000 linhas; opcodes 114 → 69; tipos de
objecto 17 → 7. fib(30) 93 ms → 54 ms.

## 10. Próximos passos

1. Peepholes: ADDI/SUBI, comparações com imediato, intrínsecos de matemática.
2. `--debug`: bounds check por dimensão nos arrays (agora só o índice achatado).
3. Fase 5-B (ficheiros, streams, banks sobre `ObjBuffer`).
4. Listas e mapas por comandos (`CreateList`, `ListAdd`, ...), se quisermos.
