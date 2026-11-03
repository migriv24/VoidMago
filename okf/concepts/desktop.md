---
okf_version: "0.1"
type: Concept
title: Void Mago Desktop
description: The GUI as a renderer of documents the CLI produces — the boundary rule and its test, the five surfaces, what base has to grow for each, and where Void Palabra does and does not belong.
tags: [status:partial, audience:dev, confidence:high]
timestamp: 2026-09-04T00:00:00Z
---

# Void Mago Desktop

A Void Maiz host over `libvoidmago`, exactly as Void Hormiga is a Void Maiz
host over its own engine. **Second**, and deliberately so: the CLI is the one
front end that must always work, because the tool that repairs a broken install
cannot need a working install to run.

> **Status, 2026-09-04: it builds and runs.** All four surfaces below are
> drawn; the Family tab authors `family.json` through the same library call the
> CLI uses; the Graph tab projects the family into a Void Core mantle and hands
> it to Void Maiz's canvas. Built behind `-DVOIDMAGO_BUILD_DESKTOP=ON`, which
> is **off by default** so a clone with no siblings still builds the CLI with
> zero dependencies. `voidmago-desktop --check` is the window's smoke test with
> the window taken off, and it is a ctest — it also proves the CLI bridge, so
> the Command tab cannot claim a binary that is not there. Not built:
> `mago init`, the per-recipient release paragraph editor, and everything under
> credentials.
>
> **The window has a command line.** The Command tab shells out to the real
> `mago` rather than dispatching verbs through the library it already links,
> because a second argument parser would be a second idea of what the commands
> are. It is the cheapest possible enforcement of §1's rule: the GUI cannot
> grow a verb the CLI lacks, because the GUI does not implement verbs.

The author's framing, which is the whole design:

> honestly, void mago desktop should not be doing a whole lot. or like, no logic
> or engines should be on void mago desktop.

# 1. The rule, and the test that enforces it

> **Void Mago Desktop renders documents Void Mago produces, and edits one
> document Void Mago writes.** Every verdict, ordering, version comparison,
> validation and layout decision happens in `libvoidmago` and crosses to the
> GUI as JSON.

The test, which is mechanical enough to settle arguments:

> **Delete Desktop and no answer changes. Delete the CLI and nothing works.**
> If the GUI can compute something the CLI cannot report, it is in the wrong
> binary.

A second, sharper form: **every pixel in Desktop must be traceable to a field
in a document Mago printed.** Not "derivable from" — *present in*. A GUI that
sorts by a number it computed itself is a GUI with a second implementation of
that number, and the second one is the one somebody will be reading when they
disagree.

## Why this is not merely tidiness

Void Maiz already works this way over Void Core, and says so:

> It owns no model state of its own. The graph you see is a *projection* of the
> core's state document; the log of what you did *is* the interaction.

For Mago there is a harder reason. **Desktop links Void Maiz, which links Void
Core. Base links nothing.** So any logic that lives in Desktop is logic that
cannot run when Void Core is broken — and diagnosing exactly that is Mago's
job. Logic in the GUI would mean the answer on a headless server and the answer
on a workstation could differ, and the entire value of the compatibility matrix
is that the answer is the same everywhere.

Corollary: **the GUI gets no features the CLI lacks.** Not a policy, an
architecture — the GUI has no way to implement one.

# 2. The five surfaces

| tab | what it renders | what base gives it | state |
|---|---|---|---|
| **Family** | the interaction matrix | `mago matrix --json` | — |
| **Graph** | the same data as a canvas | `mago matrix --json`, `mago graph --json` | — |
| **Registry** | folders, and what is missing a manifest | `mago scan --json`, `mago register`, `mago init` | roots |
| **Release** | blast, and the broadcast | `mago blast --json`, `mago notify --json` | — |
| **Build** | plan, installer, later credentials | `mago plan --json`, `mago wizard` | credentials |

Plus one thing that is **not** a tab: a **doctor strip** along the bottom,
always visible, showing the current finding counts. Health is context, not a
destination — you want to see "2 errors" while you are looking at the pair that
caused them, not after navigating to a page about it.

## Family — the table

`mago matrix --json` hands over the axis order, the strict upper triangle, and
per-project diagonal rows. Desktop draws the grid, and that is all it draws:
the glyph for a cell, its colour, and the arrow's direction come from `bond`,
`cost` and `direction` fields that are already in the document. Clicking a cell
opens the pair; typing a note calls `mago pair a b --note "..."`.

**This is the reason the GUI is worth building.** The matrix has 21 cells today
and will have 91 when the seven unmanifested projects have manifests. Authoring
those from a terminal means typing two project names per cell. Authoring them
from a table means clicking the cell you are already looking at. The tool is
not doing anything new; the friction is a hundredth.

Only the upper triangle is sent. A consumer that wants cell `(j,i)` reads
`(i,j)` and negates `direction` — see [interaction matrix](interaction-matrix.md)
§1. Sending both halves would be sending one fact twice and inviting them to
disagree.

## Graph — the same content, projected differently

The family is not a tree and drawing it as one lies about it: `requires` is a
DAG, seams are an undirected multigraph over the same vertices, and the two
overlap. Void Maiz draws exactly this kind of thing.

The projection is mechanical and therefore allowed in Desktop: **one rune per
project, one link per pair**, `bond` as the glyph, `cost` as the edge weight,
`direction` as the wire's orientation — then hand the mantle to
`maiz::project_scene`. No decisions are made along the way; a different
projection of the same JSON would produce a differently-shaped picture of the
same facts, and none of it is an answer anybody could disagree with.

Layout is Maiz's (depth auto-layout: columns by longest directed distance from
a source). Base does not need a layout engine, and Desktop must not grow one.

Void Core 0.2.14 makes the projection cleaner than it would have been:
`kind: "measure"` runes plus values-on-edges are the right shape for `cost`,
and act runes are the right shape for a seam that is a verb rather than a
noun. That is a reason to keep the authored document graph-shaped, not a reason
for base to depend on Core.

## Registry — the tab with actual work behind it

Pick a folder; know the family. Three things, and base owns all three:

1. **Roots.** Today `--root` is one folder scanned one directory deep, and it
   is a flag rather than state. A registry tab needs remembered roots, which
   means `MAGO_HOME` (`~/.mago/roots.json`) — the same shape Void Hormiga and
   FaultSack already use for their own homes. New base verbs: `mago register
   <path>`, `mago forget <path>`, `mago roots`.
2. **Scaffolding a manifest.** The largest gap in the whole project is that
   **seven Void projects carry no `void.json`**, and Mago cannot compute a
   verdict for a project it has never read — it now says so on every run. The
   registry tab's real job is turning that list into manifests: `mago init
   <path>` writes a starter `void.json` from what is on disk (directory name,
   CMake project name and version, detected language, `pyproject`/`package.json`
   metadata), and the person fills in `requires` and `interacts_with`.
   **Base writes the file; Desktop shows a form.**
3. **What it must NOT do.** Desktop never edits another project's `void.json`
   beyond that first scaffold. Each repository owns its own manifest; a central
   GUI that edited everybody's would make the registry the source of truth
   instead of the repositories, and the family's whole convention is the other
   way round. `mago init` hands a project its first manifest and then gets out.

## Release — the second authoring surface

`mago notify <project>` already produces the mechanical four fifths of a
release broadcast and leaves section 4 — *what this release means for you
specifically* — as a marked hole. That hole is a text box with a recipient's
name on it, which is precisely a GUI's job.

So the Release tab is: pick a project, see the version move its `release` block
declares, see the verdict per recipient, **write the per-project paragraph**,
and press send (`mago notify --write`). Same shape as the matrix: Mago computes
everything except the sentences, and the GUI exists to make the sentences cheap
to write.

A blast preview belongs here too — a version field and a compatible/breaking
toggle over `mago blast --to X --change Y --json`, which is a slider over a
function base already exposes.

## Build — and the credentials question

`mago plan` and `mago wizard` are the compiler face: resolve an application,
show what gets built versus the much smaller set that gets installed, emit the
installer. All present, all `--json`.

**Credentials** arrive with signing, and Void Hormiga's Antfarm is the model:
credentials live in each node's encrypted config, not in the UI's state. The
rule here is the same one as everywhere else on this page, plus one:

- **base owns the keystore.** `MAGO_HOME/credentials.enc`, one signing identity
  for the whole family (see [distribution](distribution.md) §signing), read
  through base and never held in Desktop's own model.
- **the CLI can do everything the GUI can**, because signing has to work in CI
  where there is no GUI at all. If the GUI is the only way to sign a release,
  releases stop being reproducible.
- **no secret is ever a field in a rendered document.** `mago plan --json` and
  the receipt must stay safe to paste into a bug report, which means the
  keystore is reachable by name and never by value.

# 3. What base has to grow, in order

Everything Desktop needs is a base verb, so "what does Desktop need" and "what
does Mago need" are the same list read from two ends.

1. **`--json` on every verb.** `graph` still has none. Desktop cannot render
   what it cannot parse, and a verb without `--json` is a verb the GUI would
   have to reimplement — which is how logic gets into the wrong binary.
2. **`mago register` / `mago roots` and `MAGO_HOME`.** The registry tab, and
   the end of `--root` being retyped.
3. **`mago init <path>`.** Seven manifests are missing; this is the tool that
   closes that gap, and it is worth more than the GUI is.
4. **`mago describe --json`** — the verb surface, machine-readable, the same
   way every other Void application answers *"what can I do?"*. Desktop builds
   its own menus from it and hardcodes nothing.
5. **`mago diff` — what changed since the last scan.** "Void Core moved
   0.2.13 → 0.2.14 and here is who cares" is the *updater's* face, the third of
   Mago's three jobs, and the one a GUI is genuinely better at than a terminal.
   Needs a remembered snapshot, which is `MAGO_HOME` again.
6. **The keystore and signing.** Last, because its value is reputation
   accumulated over time and the clock has not started.

Not on the list, deliberately: a build farm. It is the largest gap in the
project ([blast radius](blast-radius.md) §7) and it is not Desktop's, not
base's, and not a GUI feature.

# 4. Void Palabra: not in base, ever; in Desktop, not yet

Asked directly, and worth answering in three parts.

**Base: never.** The constraint is already written down and this does not
change it — *"it links nothing of the family's: not Void Core, not Void
Palabra, because anything it links is something that can be broken underneath
it."* Mago's inputs are other people's files on disk and its outputs are
documents that must be readable by a tool with no Void dependencies at all. A
storage layer under that would be a bootstrap where none is needed.

**Desktop: the case is real, and it is not yet.** Desktop already carries Core
and Maiz, so Palabra costs it nothing structural. And `family.json` is exactly
Palabra's problem: authored content, hand-edited, that will be edited on two
machines the moment there are two people — and *"independent changes genuinely
have no order and inventing one is what makes merge hard"* is a description of
two people annotating different cells of the same table. For **one** developer
on **one** machine it buys nothing, and a tool built for a user who does not
exist yet is that user's build script wearing a project's clothes.

**What to do now, at zero cost:** keep `family.json` graph-shaped — projects
and pairs, stable identity per pair, no positional meaning — so it projects
losslessly into a Core mantle later. That is already true, it is what the Graph
tab wants anyway, and it means adopting Palabra is a decision rather than a
migration.

**The tempting wrong answer** is Palabra as Mago's storage layer generally.
Mago is not storing a document a person authored over years; it is reading
manifests that other repositories own, and the moment those become a synced
store, the repositories stop being the source of truth.

Void Rancho, the family's database since 2026-10-30, is the same answer
again. A registry of twenty manifests and one authored table is not a
workload for a database, and a store would make Mago's copy the thing people
trust instead of the repositories.

Void Allomone is the same answer for a different reason: nothing in Desktop
derives anything, so there is nothing for a logic DSL to do. Both stay
declined until something asks for them.

# 5. Why the GUI is second and not optional

The interaction table is the author's larger goal — applications that share
whole functionality rather than merely opening each other's files — and it is
**human-authored, one row per pair**. Mago can compute which pairs exist, what
they cost, and which of them two manifests disagree about. It cannot write the
sentence, and the sentence is the point.

Everything in this document is in service of making that sentence cheap to
write and impossible to lose. That is what a GUI is for here. It is also why it
can afford to contain no logic at all: **the scarce thing is not computation,
it is the person's attention**, and every line of logic in the GUI is a line
that has to be right in two places.

See also [[interaction-matrix]], [[blast-radius]], [[broadcast]],
[[distribution]].
