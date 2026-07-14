# Haiku coding style for driver authors

Haiku enforces a strict, distinctive style. Match it exactly — reviewers do, and
so should generated code. Authoritative source:
<https://www.haiku-os.org/development/coding-guidelines>. This file distills the
parts that matter for kernel drivers and states the one place Jidō Renga
deliberately differs (the license header).

## Formatting

- **Indent with tabs**, and treat a tab as **4 columns** wide.
- **100-column** line limit.
- Function definitions put the **opening brace on its own line**; control
  statements (`if`, `for`, `while`, `switch`) keep the brace on the **same
  line**.
- **Two blank lines** between function definitions (one inside a function to
  group logic).
- Spaces inside control keywords: `if (x)`, not `if(x)`; no space for calls:
  `foo(x)`.
- Pointer/reference binds to the type: `FooDevice* device`, `const char* name`.

```cpp
status_t
FooDevice::Read(off_t position, void* buffer, size_t* _length)
{
	if (buffer == NULL)
		return B_BAD_VALUE;

	// ...
	return B_OK;
}
```

## Naming

- **Types and functions**: `InterCaps` (leading capital) —
  `FooDevice`, `RegisterChildDevices`.
- **Variables**: `interCaps` (leading lower) — `deviceCount`, `openMode`.
- **Sigils** (this is the Haiku tell):
  - `fMember` — instance member (`fLock`, `fIRQ`).
  - `gGlobal` — global.
  - `sStatic` — file-static (`sDeviceManager`, `sDriverLock`).
  - `kConstant` — constant / enum value (`kMaxObjects`).
  - `_privateMethod` and `_outParameter` — leading underscore for private
    methods and for output parameters (`size_t* _length`).

## Language rules

- Use `SupportDefs.h` fixed-width types: `int32`, `uint8`, `status_t`, `off_t`,
  `bigtime_t`, `size_t` — not bare `int`/`long`.
- Return `status_t` and the `B_*` error codes (`B_OK`, `B_NO_MEMORY`,
  `B_BAD_VALUE`, `B_ERROR`); don't invent errno-style ints.
- **C++ casts only** (`static_cast`, `reinterpret_cast`, `const_cast`) — never
  C-style casts in new code.
- `NULL`, not `0`, for pointers.
- **No `else` after `return`** — dedent the alternative.
- **No `goto`.** Use early returns + RAII (see cpp-patterns.md).
- **No Yoda conditions** — write `if (device == NULL)`, not `if (NULL ==
  device)`.
- Prefer stack allocation and constructor **initializer lists** over assignment
  in the body.
- Kernel C++ has **no exceptions and no RTTI** — build with `-fno-rtti`, use
  `new(std::nothrow)`, and check for `NULL`.
- Prefer C-style standard headers: `<stdio.h>`, `<string.h>` — not `<cstdio>`.

## Include ordering

Groups, separated by a blank line, **alphabetized within each group**:

1. Your own header first (`FooDevice.cpp` includes `"FooDevice.h"` first).
2. C / POSIX system headers (`<stdio.h>`, `<string.h>`).
3. Haiku public headers (`<device_manager.h>`, `<KernelExport.h>`).
4. Haiku private headers.
5. Local project headers (`"Driver.h"`).

## Comments

- Doxygen for API/interface documentation. File/function doc uses `/*!` … `*/`
  or `//!` for the brief.
- Comment contracts, invariants, hardware meaning, units, and non-obvious safety
  requirements. Do not narrate debugging history, discarded approaches,
  external source-reading, or obvious code.
- Section banners are fine sparingly; keep them short.

## Tooling

- **`haiku-format`** — the project's `clang-format` wrapper. Run it on every
  file; it encodes most of the formatting rules above.
- **`src/tools/checkstyle/checkstyle.py`** — flags style violations
  `haiku-format` doesn't (naming, Yoda, `else`-after-`return`, etc.). Run it
  before you consider a driver done.

## The Jidō Renga header (the one deliberate difference)

Haiku's in-tree files carry a `Copyright <year>, Haiku, Inc.` block. **Jidō Renga
is not Haiku** and must not pretend to be. Every source file we author instead
carries an [SPDX](https://spdx.dev/) header — GitHub-native, REUSE-compliant, and
honest about provenance:

```cpp
// SPDX-FileCopyrightText: 2026 The Jidō Renga Authors
// SPDX-License-Identifier: MIT
// SPDX-FileContributor: Generated with GitHub Copilot
```

- **Copyright** is always `The Jidō Renga Authors` (no personal names).
- **License** is `MIT` (see the root `LICENSE` and `LICENSES/MIT.txt`).
- **`SPDX-FileContributor`** records the AI provenance honestly, and it varies by
  who wrote the file:
  - `Generated with Qwen 3.6` — the original vibe-coded drivers (Qwen Coder).
  - `Generated with GitHub Copilot` — files authored/contributed via Copilot.

  Use the tag matching the tool that actually wrote the file. Do **not** copy
  Haiku's attribution blocks — cloning them is fake provenance.

- Use the comment leader for the language: `//` for C/C++, `#` for Jamfiles and
  shell.
- Files that can't carry a header (binaries, generated data, plain docs) are
  covered centrally by `REUSE.toml`; the entire `skills/**` tree is covered
  there, which is why this skill's Markdown needs no inline header.

Everything else — formatting, naming, language rules — follows Haiku exactly. We
extend Haiku and honor its sensibilities; we only decline to impersonate its
copyright. See the root `AGENTS.md` for the project invariants.
