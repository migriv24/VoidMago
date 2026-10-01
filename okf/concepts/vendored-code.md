---
okf_version: "0.1"
type: Concept
title: Vendored code
description: The third-party libraries inside every Void binary — why "we vendor rather than resolve" was doing two jobs and only one of them was a good idea, and why this is not called a holiday.
tags: [status:implemented, audience:dev, confidence:high]
timestamp: 2026-09-04T00:00:00Z
---

# Vendored code

Void Mago's README has always said this:

> **Not a package manager for third-party code.** The family vendors its
> dependencies rather than resolving them. Mago resolves *Void* projects only.

That is still true, and it was doing two jobs at once:

1. deciding how third-party code is **acquired** — copied in, pinned, reviewed,
   never fetched at build time — which is a good decision and stays;
2. quietly deciding it need not be **written down**, which is not a decision
   anybody made and is the subject of this page.

The distinction arrived from the outside. Staging Void Hormiga for its first
package found `libwinpthread-1.dll` in its import table — a MinGW runtime on
the build machine and on no other — and **no manifest mentioned it, because no
manifest had a place to put it.** The tool whose entire purpose is "what breaks
somewhere else" was blind to everything in its binaries that the family did not
write.

# 1. What was already true, and unrecorded

| | |
|---|---|
| **cJSON is vendored in six repositories** | Void Core, Void Maiz, Void Palabra, Void Allomone, Void Mago, Void Hormiga. All 1.7.18 today. Nothing checks that. Several of those copies are compiled into one executable. |
| **Void Hormiga vendors seven more** | blake3, libsodium, SQLite, nlohmann/json, stb, fonts, icons — none declared anywhere Mago could read. |
| **Five repositories keep a THIRD-PARTY-NOTICES.md by hand** | and Void Mago kept none at all, which is a compliance gap in the tool whose job is finding gaps. |

Every one of those is the same shape as the problems Mago already solves for
Void projects: a fact that is true, is relied upon, and lives only in
somebody's memory.

# 2. Three failures, in order of how quietly they happen

## Drift

The same library at two versions in one family. It costs nothing until two of
those copies end up **in one binary** — and in this family they do, because
Void Hormiga statically links Void Maiz, Void Allomone and Void Palabra, each
carrying its own vendored cJSON.

Then one set of symbols wins the link and the loser's callers get subtly
different behaviour, with no error at build time and no error at run time. It
is mitigated today by six independent `CJSON_HIDE_SYMBOLS` in six independent
`CMakeLists.txt` — which is discipline, held in six places, checked in none.

`mago vendors` reports it as `DRIFT`, and `mago doctor` calls it an **error**:

> *Two copies of one library in one family is a link nobody chose. Two in one
> binary is a bug with no error message.*

## A file that must reach the machine

`link: "toolchain"` exists for exactly the `libwinpthread-1.dll` case, and it
is the one link kind that describes a file **on the build machine and nowhere
else**. Declaring it means `mago stage`'s import check has an expected set to
compare against instead of only "unknown DLL", and `mago doctor` says so
standing still.

`link: "system"` is its opposite and equally worth stating: `opengl32.dll` is
provided by Windows and must *not* ship. Neither of these is derivable from
source; both are facts a person knows.

## Notices nobody can compute from inside one repository

This is the one that cannot be fixed by being careful.

**`voidhormiga.exe` contains Void Maiz, Void Allomone and Void Palabra**, each
compiled in, each carrying its own vendored libraries. Hormiga's obligation is
to the code *in its binary* — and no file maintained inside Hormiga's
repository can see past Hormiga's own `vendor/` directory.

Mago can, because the transitive build order is what it already computes:

```
$ mago notices voidhormiga --platform windows-x64
Compiled in: Void Core, Void Allomone, Void Maiz, Void Palabra, Void Hormiga.
```

A hand-maintained per-repository notices file is structurally incapable of
being complete. A generated one is complete by construction, and wrong only if
a manifest is wrong — which is a thing `doctor` checks.

# 3. Why this is not called a holiday

The instinct to model it like one is right, and the word is taken.

Void Core's **holiday** is *"the protocol interface to an external system the
application does not own — an API, cloud DB, file host, or knowledge bundle"*,
and all real I/O is a holiday: the core does no file or network work itself. A
holiday is a **runtime adapter with an interface** (`query`, `get`, `insert`,
`describe`).

A vendored library is **source code compiled in**. It has no interface of its
own, does no I/O on the family's behalf, and is not reached — it is *inside*.

What the two share is the modelling instinct: **name the external thing
explicitly rather than letting it be implicit.** That is worth saying out loud
and is not a reason to reuse the noun. Void Core set the precedent when it
declined to call its rune kinds γ/δ/ε because Void Maiz already used those
letters in Lafont's sense:

> Two sibling projects using the same three letters for different things, both
> about glyphs, in one stack, would have been a confusion created on purpose.

So: `vendors[]`.

# 4. The field

```json
"vendors": [
  {
    "name": "cJSON",
    "version": "1.7.18",
    "license": "MIT",
    "upstream": "https://github.com/DaveGamble/cJSON",
    "path": "vendor/cJSON.c",
    "version_source": "vendor/cJSON.h",
    "link": "source",
    "note": "…"
  }
]
```

- **`name`** is spelled as its authors spell it, and grouped across the family
  case- and punctuation-insensitively — drift must not be able to hide behind
  `cJSON` versus `cjson`.
- **`version`** is free text, because third-party versions are not all semver.
  Its whole value is comparison against the other copies, so any stable
  spelling works as long as everybody uses the same one.
- **`version_source`** points at the vendored file carrying the upstream's own
  version literal, and `doctor` checks the declared version against it — either
  literally (`#define SQLITE_VERSION "3.45.1"`) or split across macros the way
  cJSON writes it (`MAJOR 1` / `MINOR 7` / `PATCH 18`). The match is loose on
  purpose: it *confirms* a declared version rather than deriving one, so a
  false "cannot confirm" is a note somebody reads and a false "confirmed" costs
  nothing that was not already wrong.
- **`path`** covers a library's several files: one entry for `vendor/cJSON.c`
  accounts for `vendor/cJSON.h` beside it.

# 5. Adoption costs nothing to start

`mago vendors` reads what is on disk and reports what no manifest accounts for,
so the gap is visible before anybody writes a line:

```
UNDECLARED -- on disk, in no manifest
  voidhormiga     vendor/blake3
  voidhormiga     vendor/libsodium
  voidmaiz        vendor/glfw
  …
```

Then declare them, cheapest first: the ones that **ship** (a wrong answer stops
the application starting), the ones that are **vendored in more than one place**
(cJSON, where drift is a real hazard), and the rest whenever their licence
needs to appear in a notices file.

# 6. What this deliberately does not do

**It does not resolve, fetch, update, or check for vulnerabilities.** The
family vendors on purpose, and nothing here ever changes a byte in anybody's
`vendor/` directory. This is an inventory of what was vendored — the same
relationship Mago has to every other fact in the family: it reads manifests and
tells you when they disagree with each other or with the disk.

A vulnerability feed would be a different tool with a network dependency and an
external source of truth, and it is not this one.

See also [[shipping]] for where `toolchain` came from, [[blast-radius]] for the
link kinds it borrows, and Void Core's own `concepts/holiday.md` for the word
this page declined.
