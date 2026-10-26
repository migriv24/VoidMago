# Void Mago

**The keeper of the Void family.** Knows which versions of which Void projects
work together, installs them onto real machines, and tells a person when
something new exists — never silently.

```
mago scan                     every Void project that carries a manifest
mago graph                    the family's edges, dependencies and seams apart
mago doctor                   check the manifests against each other
mago plan   <application>     resolve one application into an install plan
mago blast  <project>         who is affected if this changes, and how badly
mago notify <project>         the whole broadcast a release has to send
mago matrix                   every pair of projects, in one table
mago vendors                  third-party code the family uses and does not own
mago notices <application>    the notices for everything in its binary
mago pair   <a> <b>           one pair, and where you write what you know
mago register <folder>        remember a folder holding Void checkouts
mago stage  <application>     gather the files an installer needs
mago feed   [apps...]         the update document a client reads
mago wizard [apps...]         emit the installer from the resolved plans
```

C++20, CMake, and one vendored cJSON — the same one every other Void repo
carries. **Not** built on Void Core and **not** on Void Maiz: an updater that
needs the libraries it is updating cannot repair a broken install, and that is a
bootstrap the family cannot afford.

`mago` links its C++ runtime statically, so it is a single native binary with
nothing to install first. That is the property that lets it run on a machine
where the thing it is fixing does not.

## The idea in one paragraph

Void Mago does not install anything. It **compiles `void.json` into an
installer**. Staging, elevation, atomic swap, uninstall entries and delta
patching are solved problems with maintained implementations; the part nobody
else can compute is *which files belong in the package and what breaks when one
of them moves*. So the registry and the resolver are written here, and the
result is handed to a real installer engine — [NSIS](https://nsis.sourceforge.io/)
today, [Velopack](https://github.com/velopack/velopack) next.

## The question it exists to answer

> i make an edit to void core, and it passes all the tests. but what if that
> slightly affects something somewhere else?

Not everything downstream. It depends on **how** each consumer is bound, and
every `void.json` already records that in `requires[].link`:

```
$ mago blast voidcore --to 0.2.13
Void Core 0.2.12 -> 0.2.13   change is compatible

RESHIP   -- ship the new file, do not rebuild
  Void Hormiga       links voidcore as a shared library and the change is additive
  Void Maiz          links voidcore as a shared library and the change is additive

RETEST   -- re-run the contract, nothing rebuilds
  Void Palabra       meets voidcore at the c-abi seam and links nothing of it
```

Five verdicts, cheapest first:

| verdict | when | cost |
|---|---|---|
| **untouched** | no edge at all | nothing |
| **retest** | reached through a *seam* — a transcript, a conformance case, a JSON document — or linked `import` across an additive change | re-run the contract |
| **reship** | linked `shared`, and the change stayed inside its compatible line | drop in the new file |
| **rebuild** | linked `static`/`source`, or the line broke | build it again, and anything statically bound to *it* |
| **blocked** | the new version violates a floor the consumer declared | fix the constraint first |

`import` is the fifth link kind, and it is there because `source` was lying
about somebody. FaultSack is a Python application over Void Core's Python
surface: nothing compiles, nothing of Core's ships beside it, and `source` had
Mago reporting a **rebuild of a binary FaultSack does not have.** Under
`import`, an additive Core release is a retest and a breaking one is a port.

And a project Mago has never read is reported as **unverified** rather than
quietly omitted — an absent row reads as "unaffected", which is how three of
the verdicts in the last hand-written release broadcast came to be guesses.

`rebuild` is the only verdict that propagates, because it is the only one where
a project's own build output changes. A reship leaves every binary
byte-identical, so nothing downstream can be affected by it. That single rule is
the difference between "a Core patch means rebuilding thirty applications" and
"a Core patch means shipping one DLL."

## What lands on the machine is much less than what gets built

```
$ mago plan voidhormiga --platform windows-x64
build order
  1. Void Core 0.2.12  (shared)
  2. Void Allomone 0.1.0  (static)
  3. Void Maiz 0.1.0  (static)
  4. Void Palabra 0.1.0  (static)
  5. Void Hormiga 0.1.0

files on the machine
  Void Core          libvoidcore-win-x64.dll
  Void Hormiga       voidhormiga, voidhormiga-cli, AGENT-GUIDE.md, okf/, fonts/

compiled in -- no file of its own, but a rebuild if it changes
  Void Allomone 0.1.0, Void Maiz 0.1.0, Void Palabra 0.1.0

notes
  voidcore: floor is >=0.2.12 (from voidhormiga, voidmaiz), which raises the
  looser >=0.2.7 declared elsewhere
```

Five projects build; one runtime file ships. A statically linked dependency is
already inside the consumer's binary, and listing it as something to install
would describe an install that does not exist. That last note is the other half:
Hormiga asks for Core `>=0.2.7`, but Void Maiz — which Hormiga also uses — asks
for `>=0.2.12`, so Hormiga's real floor is 0.2.12 and nothing in Hormiga's own
repository says so.

## Who uses Mago, and the commands for each

No user of a Void application ever needs Mago: every application ships complete,
and on a phone the store is the only updater. Mago is for the people who BUILD
the applications. See [okf/concepts/audiences.md](okf/concepts/audiences.md).

```
mago report voidhormiga      a fork maintainer or contributor: the stack, and where to report
mago upstream voidhormiga    a fork maintainer: upstream releases since this fork's base
mago learn voidhormiga       a new contributor: what to read, and FaultSack's study site
```

A manifest may now state `identity` (`app_id`, `publisher`, `feed`,
`belongs_to`), `fork_of` and `contribute` (`guide`, `security`, `issues`).
`mago doctor` warns when a fork ships with upstream's identity, because a phone
or an installer would take it as an update to the original.

## `mago bootstrap` — the family onto a disk that has nothing

A contributor to an application needs its libraries beside it, as siblings, at
versions that work together. (The `v0.2.14` line is what it prints once Void
Core tags its releases; on 2026-10-24 none of the libraries has a tag.)

```
$ mago bootstrap ./VoidHormiga
clone    voidcore  v0.2.14  -> ../VoidCore
         the newest release >=0.2.14 allows
clone    voidmaiz  default branch  -> ../VoidMaiz
         no release tags: the default branch, which no matrix has approved
absent   voidhammer (optional)  default branch  -> ../VoidHammer
         no repository is named, so it is left out; voidhormiga builds without it
```

Each `requires[]` entry may carry a `repository` (the edge has to, since the
dependency's own manifest is inside the repository nobody has cloned yet) and
`"optional": true`. The chosen release is the greatest tag inside the
intersection of every requirer's range. Anything already on disk is read and
judged, never replaced. `--dry-run` decides without cloning; `--full` clones
dependencies with full history. See [okf/concepts/bootstrap.md](okf/concepts/bootstrap.md).

## `mago notify` — the part a verdict is not

A verdict is not a release message, and we know that because one got written
without it. Void Core shipped 0.2.14 by reading eight sibling repositories by
hand and writing eight messages into eight repository roots — then wrote down
what the verdict alone had failed to carry:

> 1. **The verdict.** Mechanical, from the manifests.
> 2. **Behavior changes that are not ABI changes.** *This is the category a
>    dependency resolver structurally cannot see*, and it is the one that bites.
> 3. **New optional surface.** A resolver has nothing to say here; a notifier does.
> 4. **Per-project relevance.** Not derivable from a manifest, and the most
>    valuable part of every message.
> 5. **Where to read the rest.**

`mago notify` produces 1, 2, 3 and 5, and leaves 4 as a hole with a comment in
it. `--write` puts one addressed message in each affected project's root, named
the way the family already names them by hand.

```
$ mago notify voidcore
Void Core 0.2.13 -> 0.2.14   compatible   (declared by the release block)
three rune kinds, the glyph split, declarations that travel, and values on edges

the verdict                     mechanical, from the manifests
  RESHIP      Void Hormiga, Void Maiz
  RETEST      FaultSack, Void Palabra
  UNTOUCHED   Void Allomone, Void Mago

behavior changes (4)            what a resolver cannot see
  * `link --weight` now refuses a non-number...
    affects: any code that depended on that silent zero; it depended on a bug.
  ...

cannot address (3)              no manifest here: no verdict, no message
  voidloopsstudio   c-abi
  voidreyna         transcript
  voidunity         conformance-cases
```

### The one bit a human supplies

Every verdict above is derivable from the manifests **plus one fact**: that the
change was additive. That fact is now a field, so it travels with the release
instead of living in the head of whoever is publishing it:

```json
"release": {
  "previous": "0.2.13",
  "change": "compatible",
  "summary": "three rune kinds, the glyph split, declarations that travel",
  "adds": ["glyph", "measure", "values"],
  "behavior_changes": [
    { "what": "link --weight now refuses a non-number",
      "who_is_affected": "any code that depended on the old silent 0.0" }
  ],
  "read_more": ["SPEC.md", "okf/concepts/quantity.md"]
}
```

Each `behavior_changes` entry is an **object**, and a wrong shape is refused by
name rather than skipped: a bare string, or an entry with no `what`, names the
file and the index and stops the command. Void Hormiga 0.1.1 wrote four of them
as strings, `mago feed` reported success, and the feed said
`"behavior_changes": []` — on the one field whose absence nothing else in the
manifest can reveal. A dropped `adds` entry is a missing bullet; a dropped
behavior change is the warning that never reached the person being asked to
update. The same rule runs one level up: a manifest Mago could not read is
printed on stderr before any command speaks, and no command exits 0 over a
family with a hole in it.

With it, `blast` and `notify` need no flags at all, and Mago reports whether it
used a declared fact or an inference — because a verdict is only as good as the
fact underneath it. See [okf/concepts/broadcast.md](okf/concepts/broadcast.md).

## `mago doctor` checks the two things a hand-rolled release got wrong

The 0.2.14 message named both of them, and both are now checks:

- **A repository that contradicts its own manifest.** Void Core shipped 0.2.14
  with a `void.json` still reading `0.2.12` — stale by two releases, because
  nothing read it against anything. `doctor` now checks the manifest's version
  against `CMakeLists.txt`, `pyproject.toml`, `package.json` and `Cargo.toml`,
  and calls a disagreement an **error**: every verdict about a project is
  arithmetic over that number, so a stale one does not produce a missing
  answer, it produces a confident wrong one. A project that versions a
  subcomponent separately narrows the check with a top-level `version_sources`
  array, or turns it off with an empty one.
- **A partner nobody re-read.** A project named in `interacts_with` that has no
  manifest in the registry is now reported, standing still and in every blast.

## The interaction matrix

Every pair of Void projects in one table, because the useful question is almost
never about one project:

```
$ mago matrix
7 projects, 21 pairs, 12 bonded, 1 with a note

                   1  2  3  4  5  6  7
1  FaultSack       #  .  >  .  -  -* .
2  Void Allomone   .  #  .  <  -  <  .
3  Void Core       <  .  #  <  -  <  -!
4  Void Hormiga    .  >  >  #  .  >  >
5  Void Mago       -  -  -  .  #  .  .
6  Void Maiz       -* >  >  <  .  #  .
7  Void Palabra    .  .  -! <  .  .  #

  #  the project itself        >  the row requires the column
  .  no edge either way        <  the column requires the row
  -  a seam: data crosses, nothing links
  +  authored only: a person recorded a relationship no manifest does
  *  a note in family.json     !  the two manifests disagree
```

**The table is symmetric and the arrow is not**, which is the whole design.
Every square matrix splits uniquely into a symmetric part and an antisymmetric
one; here the *relationship* is the symmetric part — one record per unordered
pair, stored once — and the *direction* is the antisymmetric part, negated when
you read the cell from the other side. So `AB = BA` holds literally, only the
strict upper triangle is stored (**21 records, not 49 cells**), and 21 is the
number a person actually has to author.

A cell is a **join over evidence**, not a value somebody typed: bond
(`none < authored < seam < requires`) and cost
(`untouched < retest < reship < rebuild < blocked`) are chains, their product
is a lattice, and the cell is the least upper bound of everything both
manifests say. Because evidence is gathered from both sides before it is
joined, symmetry is a theorem about the computation rather than a rule anybody
has to remember. See
[okf/concepts/interaction-matrix.md](okf/concepts/interaction-matrix.md).

### `family.json` — the half no manifest can hold

```
voidcore -- voidpalabra   seam   retest   seam: c-abi   DISPUTED
```

Void Core calls that seam `c-abi`; Void Palabra calls it
`state document + command journal`. Both manifests are internally consistent
and honestly written, and neither is wrong from inside its own repository —
**because a fact about a pair belongs to neither repository.** `mago doctor`
has been reporting that contradiction since its first run with nowhere to put
the answer.

`family.json` is that place: one authored document, held by the project whose
job is the family.

```
mago pair voidcore voidpalabra              # the cell, and every piece of evidence
mago pair voidcore voidpalabra --note "..." # what you know
mago pair a b --seam NAME                   # settle a disputed name
```

Nothing in it is computed and Mago never writes a sentence into it on its own.
An authored `seam` settles a dispute — the cell stops being DISPUTED and
`doctor` still calls the manifests wrong, but now with the fix attached. An
authored note on a pair no manifest connects is the only way that relationship
can exist at all.

## Void Mago Desktop

A Void Maiz host over `libvoidmago`, and **it contains no logic at all** — a
rule with a test attached:

> **Delete Desktop and no answer changes. Delete the CLI and nothing works.**

Not tidiness. Desktop links Void Maiz, which links Void Core; the CLI links
nothing. So anything computed up there is something that cannot run on exactly
the machine Mago exists to diagnose, and the answer on a headless server would
be free to differ from the answer on a workstation. Its `App` struct holds
documents the library produced, ephemera (which cell is selected, what is typed
but not saved), and one `maiz::Core` holding a projection of the first — and
there is deliberately no fourth kind of state.

Four surfaces and one strip:

- **Family** — the interaction matrix, clickable. Selecting a cell shows every
  piece of evidence from both manifests, and the note box under it writes
  `family.json` through the same `mago::Family` the CLI uses. This is what the
  GUI is *for*: the table has 21 cells today and 91 once the seven unmanifested
  projects have manifests, and clicking the cell you are looking at beats
  typing two project names per pair.
- **Graph** — the same data on a Void Maiz canvas: one rune per project (glyph
  by kind), one link per bonded pair, direction from the pairing's
  antisymmetric part, weight from its cost. Layout, camera and selection are
  Maiz's; Desktop contributes no geometry. The family is not a tree and drawing
  it as one would lie about it.
- **Registry** — the roots this machine scans, the projects found, and **the
  gap**: the seven folders that look like Void projects and carry no manifest.
- **Release** — `blast` and `notify` for any project, with its verdict table,
  behavior changes and unaddressable partners.
- **The doctor strip** — always visible along the bottom, never a tab. A family
  that disagrees with itself should not have to be asked. It carries three
  tabs: **Findings**, **Log**, and **Command**.

**The Command tab runs the real `mago` binary.** Not a reimplementation — the
desktop links `libvoidmago` and could dispatch a verb directly, but then it
would need its own argument parser, and a second parser is a second idea of
what the commands are. Shelling out makes *the GUI can do nothing the CLI
cannot* true by construction rather than by discipline, and it answers the
obvious question about a tool whose whole argument is that the terminal comes
first: why would its window not have one.

**Zoom** is one control for the whole interface — `Ctrl` + wheel, `Ctrl` `+`/`-`,
`Ctrl` `0`, or the buttons in the header. The canvas keeps its own camera,
because "the words are too small" and "I want to see more of the graph" are
different complaints. **`?`** opens a help window covering what a seam is, what
`shares` means, how the verdicts work, and how these projects are meant to
relate to each other; the same explanations hang off `(?)` markers beside the
fields themselves.

Node shape carries the project's kind — a **hexagon** is a runtime (a substrate
everything sits on), a **circle** is a capability (no orientation: you develop
with it rather than install it), a **windowed body** is an application. Shape is
a second channel on purpose, because colour must never be the only signal.
Both are Void Maiz `hints` keys; Desktop draws no geometry of its own — which
is also why its loose-wire humps are
[reported to Void Maiz](../VoidMaiz/MESSAGE_FOR_VOIDMAIZ_mago-wire-routing-for-loose-graphs-2026-09-04.md)
rather than worked around here.

```
cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DVOIDMAGO_BUILD_DESKTOP=ON
cmake --build build-desktop
build-desktop/bin/voidmago-desktop
```

**`-DVOIDMAGO_BUILD_DESKTOP=ON` is off by default and that is architectural**:
a clone with no siblings checked out still configures and builds `mago` with
zero dependencies. Desktop needs Void Maiz beside this repo (which finds Void
Core itself, and needs it built).

`voidmago-desktop --check` runs everything the window does except the window —
reads the documents, projects the mantle, prints what the library said, exits
non-zero if anything complained. It is a ctest, because a build machine has no
display.

### A shortcut

```
powershell -ExecutionPolicy Bypass -File scripts\make_shortcut.ps1
```

Puts **Void Mago** on the desktop, pointing at `scripts\VoidMago.cmd` rather
than at the binary so an unbuilt GUI explains itself instead of producing a
window that never appears. `-Remove` takes it off. The icon is generated by
`scripts/make_icon.py` — pure standard library, because the tool that repairs a
broken install should not need Pillow to draw its own icon; the motif is an
interaction matrix with its diagonal picked out and its off-diagonal cells
mirrored.

## Remembering where the checkouts are

```
mago register <folder>        remember a folder holding Void checkouts
mago roots                    the folders this machine knows
mago forget <folder>          stop scanning one
```

`--root` is a flag, and a flag is retyped. `MAGO_HOME` (default `~/.mago`)
holds one thing: the list of roots. Not a cache and not a mirror of the
manifests — **the repositories are still the truth**, and everything derived is
recomputed on every run, because a registry that remembered what a manifest
said would be a registry that could be wrong about it. Several roots are
scanned into one registry; the family is one graph however many folders it is
spread across. `--root` still wins, and a machine with nothing registered
behaves exactly as it did before there was a home.

## Getting an application onto somebody else's machine

```
mago plan   voidhormiga --platform windows-x64   what belongs in the package
mago stage  voidhormiga --platform windows-x64   put those files in one folder
makensis    stage/.../voidhormiga.nsi            compile the installer
mago feed   voidhormiga --base-url https://...   say what exists
```

**`mago stage` is the step that was missing.** `plan` always knew which files
belong in the package and `wizard` always wrote a script referencing them — at
paths under a `--source-root` *nothing had ever produced*. Staging walks each
project's checkout, finds the file the manifest promised, copies it into the
layout the emitter expects, and writes the script beside it with the source
root already pointed at itself. One command away from `makensis`.

Its value is not the copying. Run against Void Hormiga for the first time, it
found two things that would each have broken the first install on another
machine:

```
WILL NOT START -- imported by a staged binary, not in the package,
                 and not provided by Windows
  voidhormiga.exe           libwinpthread-1.dll

MISSING -- these were promised by a manifest and are not on disk
  voidhormiga     Void Hormiga declares "fonts/", which is not in ../VoidHormiga
```

The second is a stale manifest. The first is better: `libwinpthread-1.dll` is a
MinGW runtime living in the developer's toolchain directory and **on no other
machine in the world**, and no manifest mentions it because no manifest
*could* — it is a fact about how the binary was linked, not about what the
project declares. So staging reads the PE import directory of everything it
stages and reports any DLL that is neither in the package nor provided by
Windows. That is the founding question — *it passes its own tests, but what
breaks somewhere else* — asked of a binary instead of a manifest.

### Three names for one file

| | example | who uses it |
|---|---|---|
| the **build output** | `libvoidcore.dll` | the compiler wrote it |
| the **release asset** | `libvoidcore-win-x64.dll` | a manifest declares it; a release page holds every platform at once |
| the **installed file** | `libvoidcore.dll` | what the consumer's binary asks the loader for |

The platform tag keeps three builds apart in one folder on a download page and
**must not survive onto the machine**: an executable linked against
`libvoidcore.dll` does not start beside a file that still carries the tag.
`installed_name()` is that rule, read by the stager and the emitter both. The
NSIS test that used to assert the tagged name was asserting a bug.

## Updating: the hub is a file

**Standalone installers, and one static `void-updates.json` beside them.** A
hub application is an application you must install before the application you
actually wanted — the same bootstrap Mago refuses for itself — and what a hub
would give a *user* is either unnecessary (a shared runtime cache: side-by-side
installs each carry their own 249 KB `libvoidcore.dll`, and shared runtimes are
what DLL hell is made of) or is a list, and a list is a file.

`mago feed` writes it, and it carries what a version number cannot:

```json
"releases": [{
  "version": "0.1.1",
  "change": "compatible",
  "summary": "...",
  "behavior_changes": [{ "what": "...", "who_is_affected": "..." }],
  "artifacts": { "windows-x64": {
    "file": "VoidHormiga-0.1.1-windows-x64-setup.exe",
    "url": "...", "bytes": 24117248, "sha256": "...", "signature": null } }
}]
```

`behavior_changes` comes straight from the `release` block, so the prompt a
person sees can be *"`link --weight` now refuses a non-number — anything
relying on the old silent 0.0 is affected"* rather than *"a new version is
available"*. **That is what "never silently" means in practice.**

Mago is a **build-time** tool: it never runs on a user's machine, and an
updater that needed Mago installed would be one more bootstrap. Fetching the
feed, comparing versions, asking the person, verifying the download and
launching the installer are the application's — see
[okf/concepts/shipping.md](okf/concepts/shipping.md) for the full split.
`signature` is present-and-null because signing is intended and not yet built.

## What else is in these binaries

`requires` is a Void project. **`vendors[]` is everything else** — cJSON,
SQLite, GLFW, Dear ImGui, libsodium, and the toolchain runtime that arrives
whether anybody asked for it or not.

"We vendor rather than resolve" was doing two jobs: deciding how third-party
code is *acquired* (copied in, pinned, reviewed — a good decision, and it
stays), and quietly deciding it need not be *written down*, which nobody
actually decided. The second one broke a package: `libwinpthread-1.dll` was
found in Void Hormiga's import table and mentioned in no manifest, **because no
manifest had anywhere to put it.**

```
$ mago vendors
1 libraries, 1 declarations across 7 projects

  cJSON             1.7.18        MIT         1 project
                    voidmago (source)

UNDECLARED -- on disk, in no manifest
  voidhormiga     vendor/blake3
  voidhormiga     vendor/libsodium
  voidmaiz        vendor/glfw
  ...
```

Three things it catches, in order of how quietly they happen:

**Drift.** cJSON is vendored in six repositories. All 1.7.18 today, by
discipline, checked by nothing — and several of those copies are compiled into
one executable, where one set of symbols wins the link and the loser's callers
get different behaviour with no error anywhere. `doctor` calls it an error:
*two copies of one library in one family is a link nobody chose; two in one
binary is a bug with no error message.*

**Files that must reach the machine.** `link: "toolchain"` is the dangerous
kind — a file on the build machine and nowhere else. `link: "system"` is its
opposite (`opengl32.dll` is Windows', and must not ship). Neither is derivable
from source; both are facts a person knows, and now has somewhere to write.

**Notices no single repository can compute.** `voidhormiga.exe` contains Void
Maiz, Void Allomone and Void Palabra, each compiled in with its own vendored
libraries, so Hormiga's obligation runs past its own `vendor/` directory and no
file maintained inside Hormiga can see that far. `mago notices <app>` walks the
transitive build order:

```
$ mago notices voidhormiga --platform windows-x64
Compiled in: Void Core, Void Allomone, Void Maiz, Void Palabra, Void Hormiga.
```

This repo's own `THIRD-PARTY-NOTICES.md` is generated that way — it had none at
all before, which was a compliance gap in the tool whose job is finding gaps.

**Not called a `holiday`**, though the modelling instinct is the same one.
Void Core's holiday is a runtime interface to an external *system*, where all
real I/O lives; vendored source has no interface and does no I/O — it is
inside, not reached. Same instinct, different referent, and reusing the noun
would be the collision Void Core avoided when it declined γ/δ/ε because Void
Maiz already used those letters. See
[okf/concepts/vendored-code.md](okf/concepts/vendored-code.md).

**Still not a package manager.** Nothing here resolves, fetches, updates or
scans for vulnerabilities, and nothing ever changes a byte in anybody's
`vendor/` directory.

## `void.json`

Every Void project declares what it is and what it needs, in a machine-readable
manifest at its repository root. It is the whole input to this tool, and the
only part of Mago that is expensive to retrofit — which is why it was adopted
while there were six repositories to adopt it rather than thirty.

Schema: [schema/void.schema.json](schema/void.schema.json). Reference:
[`../VoidHormiga/void.json`](../VoidHormiga/void.json).

Two properties matter more than the exact field names:

- **`requires` is a dependency edge.** It names the CMake variable, so a
  resolver can point a build at a specific checkout without editing anybody's
  `CMakeLists.txt`.
- **`interacts_with` is not.** Void Reyna emits a transcript that Void Hormiga
  replays; neither links the other. Keeping those apart is what stops the
  compatibility matrix from inventing edges — and what makes `retest` a
  distinct verdict from `rebuild`.

## Two front-ends, and the order matters

- **`mago` (CLI) — the real one.** Dependency-free, by hard constraint.
- **Void Mago Desktop — a Void Maiz host on top,** exactly as Void Hormiga is.
  Four surfaces and a doctor strip, and **no logic at all** — see below.
  Designed in [okf/concepts/desktop.md](okf/concepts/desktop.md); the Family,
  Graph, Registry and Release tabs build and run.

## What it is not

- **Not Void Palabra.** Palabra moves *data between peers* at runtime. Mago
  moves *software versions onto machines* at install time.
- **Not the Antfarm.** The Antfarm is an operator configuring their own
  services. Mago is the family maintaining itself.
- **Not a package manager for third-party code.** The family vendors its
  dependencies. Mago resolves *Void* projects only.
- **Not a mobile updater.** App stores forbid an app shipping its own binary
  updater; there, the store *is* the updater. The registry still applies.

## Building it

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && ctest --output-on-failure
```

The binary lands at `build/bin/mago`. `--root` is the folder holding the Void
checkouts, and defaults to this repo's parent:

```
build/bin/mago --root .. scan
```

The test suite is ten smoke executables, as everywhere else in the family.
Nine build a miniature family in a temp directory, so a version bump in Void
Core cannot turn them red; `family_smoke` deliberately reads the real checkouts
and asserts shape rather than versions, skipping itself in a bare clone. The
desktop build adds a ninth, `desktop_check`, which is the GUI with the window
taken off.

## Status

`0.1.3`. The registry, the resolver, the blast radius, the broadcast, the
interaction matrix, the desktop host and the NSIS emitter work against the real
repositories — `mago notify voidcore`
reproduces the 0.2.14 release message that was written by hand, including the
three verdicts that message admitted were guesses, which it reports as
unverified rather than guessing again.

Not yet built: signing, the update client, the build farm that would make the
compatibility matrix *true* rather than merely declared, and the desktop GUI.
The largest coverage gap is unchanged and now visible from inside the tool:
**seven Void projects still carry no manifest**, and Mago cannot compute a
verdict for a project it has never read. See [okf/index.md §8](okf/index.md)
for what has to be decided first.

## License

MIT — see [LICENSE](LICENSE).
