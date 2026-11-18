# Copilot instructions for SRT

When writing or modifying code, follow the conventions in
`docs/dev/coding-conventions.md`. Key rules:

- Language standards: C90 for C parts (haicrypt, C API); C++03 for the SRT
  library; C++17 or newer for applications and tools.
- Braces open on a new line, aligned with the statement keyword.
- Indent with 4 spaces; never use tab characters.
- Pointer/reference modifiers attach to the type (`int* p`, `int& r`); no
  space before `[]`.
- Place a space before `(` after `if`, `while`, `for`, `switch`, `sizeof`
  and in local variable construction (`Condition cc (cg);`).
- No multi-assignments, and no multiple modifications in one expression.
- Put the tested value on the left of comparisons (no Yoda conditions),
  except when a function call is compared to a special return value such as
  `-1`.
- Don't use implicit bool conversion when it misleads (for example
  `if (close(s))`, `if (!strcmp(a, b))`).
- Use `const` wherever applicable.
- Mutable reference parameters in function definitions use the `w_` prefix
  (`pw_` for pointers, `aw_` for raw arrays). At the call site, wrap the
  argument in extra parentheses: `getSpeed((packetspeed))`. Always
  initialize variables passed this way.
- Naming: `PascalCase` for classes and global functions, `camelCase` for
  methods, `snake_case` for local variables, `SCREAMING_SNAKE_CASE` for
  constants.
- Fields are named `[m_|s_|g_][marker]Name` with a type/meaning marker
  (`z`, `i`, `ll`, `u`, `b`, `p`, `s`, `d`, `cb`, `a`/`ca`, `ts`/`tc`/`td`).
- Don't use the `long` type; use `int`/`int32_t` and `int64_t`/`long long`.
