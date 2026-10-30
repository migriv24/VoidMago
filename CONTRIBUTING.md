# Contributing to Void Mago

Void Mago keeps the Void family: the registry of which versions work together,
the installer it compiles from them, and the publication check that keeps every
repository clean. This page is for a person or an agent who has just cloned it.

## Getting the family onto your disk

Mago reads the other Void projects. It does not link them, so it builds alone.
Most of its answers, though, are about the checkouts beside it:

```
git clone https://github.com/migriv24/VoidMago
cd VoidMago
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build/bin/mago bootstrap ../VoidHormiga   # or any application: it and what it requires, as siblings
build/bin/mago --root .. doctor
```

`mago register <folder>` remembers a folder of checkouts, so `--root` can be
dropped. Nothing outside `MAGO_HOME` (default `~/.mago`) is written.

## Before you send a change

```
ctest --test-dir build --output-on-failure
python iscariot/iscariot.py scan . --history --strict
```

- **The CLI takes no dependencies.** Not Void Core, not Void Maiz, nothing beyond
  the vendored cJSON. The tool that repairs a broken install must not need a
  working one. The desktop host is where Maiz is allowed, and it carries no logic.
- **A test builds its own miniature family** in a temp directory (see
  `tests/toy_family.hpp`), so a release elsewhere cannot turn the suite red.
- **The OKF is the design record.** A change in what Mago decides or why is
  written into `okf/`, and the README if a command changed.

## What is never published

Every Void repository is public, and Mago's `okf/` ships beside its binary. Read
[okf/concepts/iscariot.md](okf/concepts/iscariot.md) once. The short version:

- **Correspondence stays local.** `MESSAGE_FOR_*` and `REPORT_FROM_*` files are
  how Void projects' agents talk to each other. They are gitignored, and a
  finding from one is rewritten into the OKF as a fact about Mago.
- **No client, organization or person who used a Void application** is named in
  a commit, a comment, a test or the OKF. Write "a client" or use the fictional
  examples (`acme-*`, `example.org`, `555-01xx`).
- **No machine's paths.** A sibling is `../VoidCore`.
- **Commit as your GitHub no-reply address.** Every commit publishes its author
  email.
- **No credential, ever, even a revoked one.**

`python iscariot/iscariot.py scan . --strict` catches the shapes. It cannot
catch a sentence that describes somebody, so read your diff as a stranger would.

## Reporting a problem

`mago report <project>` prints the stack as built on your machine and where that
project wants reports. For Mago itself, open an issue on this repository. Never
paste a database, a client's name or a path from your machine into one.

## License

MIT. By contributing you agree your contribution is licensed the same way.
