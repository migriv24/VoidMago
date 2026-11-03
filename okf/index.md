---
okf_version: "0.1"
type: Index
title: Void Mago
description: The front door — what Void Mago is, what it is deliberately not, and the one convention it needs from every other Void project.
tags: [status:active, audience:dev, confidence:medium]
timestamp: 2026-09-01T00:00:00Z
---

# Void Mago

**The keeper of the family.** *Mago* — magician. Founded 2026-09-01. The
registry, the resolver and the blast radius are **built and passing against the
real repositories**; see the [README](../README.md) for the commands.

One sentence: **Void Mago knows which versions of which Void projects work
together, installs them onto real machines, and tells a person when something
new exists — never silently.**

> **Status: this document is an overview, not a specification.** §4's
> convention is adopted in six repositories. §1–§7 are now implemented as
> `mago` and the vocabulary here matches the code. §8 is still the open list,
> and three of its six entries have been researched into
> [concepts/distribution.md](concepts/distribution.md).
>
> The one addition experience forced: **only a rebuild propagates.** A reship
> leaves a project's build output byte-identical, so it cannot affect anything
> downstream. That rule is the difference between "a Core patch means rebuilding
> thirty applications" and "a Core patch means shipping one DLL", and it has its
> own page at [concepts/blast-radius.md](concepts/blast-radius.md).

# 1. Why this exists

There are already twelve Void projects on disk. The author's problem, stated
plainly:

> an update to void core, void maiz, void allomone, or void palabra would very
> likely require an update to most void based applications. […] if im in charge
> of like 30+ void based applications, then i really don't want to individually
> update them all.

And the sharper half, which is the actually hard part:

> i make an edit to void core, and it passes all the tests. but what if that
> slightly effects something somewhere else?

**Installing is the easy half. The compatibility matrix is the valuable half.**
A tool that unzips a build is a zip extractor with ambitions. A tool that can
answer *"which set of versions is known-good together, and what does this Core
change break downstream"* is what lets one person maintain thirty applications.

# 2. What Void Mago is

Three jobs, in the order they are worth building:

1. **The registry.** Every Void project, what kind of thing it is, what it
   requires, what it provides, and which versions of each other they are known
   to work with. This is the foundation and everything else reads from it.
2. **The installer.** Put an application and its runtime dependencies onto a
   machine, side by side with the version already there, and never destroy a
   working install to try a new one.
3. **The updater.** Notice that something new exists, tell the person, and apply
   it **only when they choose it.** No silent updates — the same rule
   [Void Hormiga's security concept](../../VoidHormiga/okf/concepts/platform/security.md)
   already states for its own releases.

# 3. What Void Mago is not

- **Not Void Palabra.** They rhyme and they are not the same lifecycle. Palabra
  moves **data between peers**, at runtime, per document, with security.
  Mago moves **software versions onto machines**, at install time, per release.
  They share concepts and share nothing operationally.
- **Not Void Rancho.** Rancho *is* a database: it stores, indexes, queries and
  pages runes and mantles. Mago keeps no data that needs one. Its inputs are
  manifests that other repositories own, read fresh on every run, and its
  authored state is one small `family.json`.
- **Not the Antfarm.** The Antfarm is an operator configuring *their own*
  services — a database, a deploy target, a translation holiday. Mago is the
  family maintaining *itself*. An outreach organization uses the Antfarm every
  week and should never need to think about Mago.
- **Not a package manager for third-party code.** The Void family vendors its
  dependencies rather than resolving them. Mago resolves *Void* projects only.
- **Not a mobile updater, and this is a constraint rather than a choice.** App
  stores prohibit an application shipping its own binary updater. On mobile the
  **store is the updater**; Mago's role there ends at *producing* the build that
  is submitted. The registry still applies everywhere; the update mechanism does
  not.

# 4. The one thing needed now: `void.json`

**Every Void project declares what it is and what it needs, in a
machine-readable manifest at its repository root.** Roughly twenty lines. It
costs nothing today and is the only part that is expensive to retrofit — which
is why it is being adopted at the moment the repositories go public, not after.

The first one shipped with Void Hormiga (`../VoidHormiga/void.json`) and is the
working reference. The shape:

    manifest        the format version, so this file can change later
    name            machine name, lowercase        e.g. "voidhormiga"
    kind            runtime | capability | application  (see §5)
    version         this project's own version
    license         SPDX identifier
    repository      where it lives
    requires[]      other Void projects: name, version range, sibling path,
                    the CMake variable that overrides it, and how it links
    interacts_with[]  projects it exchanges data with but does not link
    provides{}      cli name, gui name, document formats, effects
    platforms[]     what it builds for today
    update{}        channel, side-by-side, signing

Two properties matter more than the exact field names:

- **`requires` is the dependency edge Mago resolves.** It names the CMake
  variable, so a resolver can point a build at a specific checkout without
  editing anybody's `CMakeLists.txt`.
- **`interacts_with` is not a dependency.** Void Reyna emits a transcript that
  Void Hormiga replays; neither links the other. Recording that separately is
  what keeps the compatibility matrix from inventing edges that do not exist.

`void.json` is the **build-time twin of `--describe`**. Every Void application
already answers "what can I do?" at runtime through its CLI briefing. This
answers "what am I, and what do I need?" before anything is built.

# 5. The three kinds, inherited

Void Hormiga's
[application boundaries](../../VoidHormiga/okf/concepts/foundation/application-boundaries.md)
already sorted Void projects by **what a user experiences**, and Mago adopts
that vocabulary rather than inventing a second one:

| kind | example | a user can use it? | what Mago does with it |
|---|---|---|---|
| **runtime** | Void Core, Void Maiz, Void Allomone | no — never learns the name | installs it *inside* an application; never lists it separately |
| **capability** | Void Palabra | no — you develop *with* it | may be an optional component |
| **application** | Void Hormiga, Void Reyna | yes | the thing a person installs, updates and sees |

## The pillars: the libraries every application stands on

The kinds above say what a user sees. A second sort decides what `blast` is
for: a few libraries sit under nearly every Void application, so changing one
of them reaches most of the family. Since 2026-10-30 there are five:

| pillar | what it owns | how applications bind it |
|---|---|---|
| **Void Core** | the structure and the command language. No I/O, by definition | shared library |
| **Void Maiz** | the view: the GUI, and one app shape for desktop and phones | static |
| **Void Allomone** | derivation: annotations and their lattice merge | static |
| **Void Palabra** | canonical bytes, content names, merge, history, sync between peers | static |
| **Void Rancho** | the database: runes and mantles stored, indexed, queried and paged at any size, clustering, and the matrix view a learned model reads | static (no application links it yet; Hormiga is first) |

**Void Rancho is the newest, and it matters as much as Palabra.** Void Core's
author ruled on 2026-10-30 (`VoidCore:okf/design/the-transition.md`, pillar
B) that the database belongs in a dedicated library one layer above Core,
because every application had been building its own. Hormiga built a SQLite
store and a cluster-paged working set; Squid copied that schema. Rancho is
that store, extracted and generalized. Its 0.1.0 keeps Hormiga's API, so
Hormiga moves by a namespace change. Palabra names and reconciles; Rancho
stores. Palabra's content hash names Rancho's snapshot segments. Neither
links the other.

Two rules hold for every pillar, because `mago bootstrap` depends on both:

- **A pillar is released, not borrowed from a branch.** `mago bootstrap`
  chooses the newest *tag* every requirer accepts. A pillar with no tag at
  the version its manifest declares is taken from its default branch on
  trust ([bootstrap](concepts/bootstrap.md)).
- **Every edge to a pillar names its repository.** Bootstrap cannot clone a
  dependency whose address nobody wrote down.

The consequence for Mago's UI: **a normal user's list has one row per
application.** Runtime dependencies are an implementation detail of a release,
and surfacing them would undo the property that makes them free.

# 6. Two front-ends, and the order matters

- **`mago` (CLI) — the real one.** It must **not depend on Void Maiz.** An
  updater that needs the GUI library it is updating cannot repair a broken
  install; that is a bootstrap the family cannot afford. CLI-first is a hard
  architectural constraint, not a phase.

  **The same argument chooses the language: C++20, statically linked, with one
  vendored cJSON** — the family's own toolchain, and a single native binary
  with no runtime to install first. An updater written in a language that must
  itself be installed before it can run cannot bootstrap the machine it exists
  to fix. It links nothing of the family's: not Void Core, not Void Palabra,
  because anything it links is something that can be broken underneath it.
- **Void Mago Desktop — a Maiz host on top,** exactly as Void Hormiga is. The
  registry browser, the version matrix, and the interaction table between
  applications. Useful, and second — and carrying **no logic at all**, by a
  rule with a test attached: *delete Desktop and no answer changes.* Desktop
  links Maiz, which links Void Core; any logic up there is logic that cannot
  run when Void Core is the thing being diagnosed. Five surfaces, what each
  needs from base, and where Void Palabra does and does not belong are in
  [concepts/desktop.md](concepts/desktop.md).

# 7. The developer face: the interaction table

The author's ask, and the reason a GUI eventually earns its place:

> i would really like a nice interface to tell me if there are conflicts across
> any of the void based applications

Two different artifacts, often confused:

- **The compatibility matrix** — which versions build and pass together. Machine
  produced, from `void.json` plus a build farm. This is what catches "Core
  passed its own tests and broke something downstream."
- **The interaction table** — how two *applications* overlap: what one can hand
  the other, which glyphs mean the same thing, what a shared asset folder
  implies. Human-authored, one row per pair.

The second is the author's larger goal — applications that share whole
functionality rather than merely opening each other's files. Void Hormiga's
boundaries page already records the correction that keeps it honest: **the GUI
is not a thin shell over an SDK.** A video editor reading a Hormiga project's
assets is reasonable; a video editor deciding what a `clearance:public` tag
means is not, because that judgement is Hormiga's domain and does not travel.

In Latin-OS terms these pairings are **port mappings**, and a boundary drawn
today becomes a process boundary later — which is the argument for writing the
table down early even while it is short.

# 8. What has to be researched, and what is now settled

Three of these have been researched into
[concepts/distribution.md](concepts/distribution.md); three remain open.

1. **The sibling-layout question.** *Answered for its first case,
   2026-10-24:* [`mago bootstrap`](concepts/bootstrap.md) clones a project and
   everything it requires as siblings, at the newest release every requirer
   accepts, and never touches what is already on disk. What is left open is
   the family's half: the libraries carry no release tags yet, so bootstrap has
   only default branches to choose from. `mago` itself resolves against
   whatever is on disk, which works for one developer and did not survive a
   second one.
2. **Version semantics.** *Settled.* Below 1.0 the minor position is the break —
   Void Core already says so in its own manifest, and `mago` infers
   `compatible` / `breaking` on that rule, with `--change` to override it when
   a version number lies.
3. **Signing and identity.** *Researched, not built.* An open-source licence
   does **not** stop SmartScreen; a signature plus accumulated publisher
   reputation does. One signing identity for the whole family, established
   early. TUF is the answer for key rotation; its maintained implementations
   are Python, Go and Rust, so the client workflow would be written against
   the spec over the libsodium the family already vendors, rather than
   imported.
4. **Applying an update on a live install.** *Open.* Windows locks a running
   binary; stage-then-swap-on-restart exists for this reason. Velopack already
   implements it, which is an argument for emitting into Velopack rather than
   writing it.
5. **The build farm.** *Open, and the largest gap.* Every verdict `mago blast`
   produces is a claim about the **manifests**, not about the **binaries**. A
   `link: shared` dependency whose ABI silently moved inside its compatible
   line reports as a reship and crashes anyway. The matrix is *declared*; only
   a farm that compiles the combinations makes it *true*.
6. **Distribution channel.** *Researched.* Own site canonical, stores
   secondary, every version kept — the Blender model, which side-by-side
   installs already commit the family to. Releases are static signed files, so
   a bucket and a CDN suffice and hosting stays a later question.

# 9. Sequencing

**Done:** `void.json` in seven repositories, FaultSack included; the registry; the resolver, which
separates what gets *built* from the much smaller set that gets *installed*; the
blast radius; the [interaction matrix](concepts/interaction-matrix.md) and the
authored `family.json` beside it — which is where that Void Core / Void Palabra
contradiction finally has somewhere to be answered; `mago doctor`, which found
it on its first run; the NSIS emitter; and — since Void Core
shipped 0.2.14 by hand and then wrote down what that cost — the broadcast:
`mago notify`, the `release` block that carries the one fact a resolver cannot
derive, and doctor's two checks for the two things doing it by hand got wrong.
See [concepts/broadcast.md](concepts/broadcast.md). And, since 2026-10-30,
[Iscariot](concepts/iscariot.md): the check that no Void repository publishes a
credential, an agent's correspondence, a private name or personal data, with a
local database of flags each project's agent answers.

**Next, in order:** a manifest in the four Void projects that still lack one
(Void Buzz, Void Hammer, Void Reyna, Void Unity) —
the registry is only as true as its coverage, and Void Reyna in particular is
already named as a seam partner by a project that can see it. Then the signing
identity, because its value is reputation accumulated over time and the clock
does not start until it exists. Then a Velopack emitter, for delta updates and
the live-install swap.

**Still not now:** the build farm, the update client, the desktop GUI. Mago has
one real consumer; a tool built for one is that one's build script wearing a
project's clothes — the mistake Allomone took two extractions to correct.

# 10. What the family has asked of Mago, and the answers

Folded in from the family's letters on 2026-10-30. The letters themselves are
correspondence, so they are not in this repository.

| asked by | ask | answer |
|---|---|---|
| Void Hormiga, 2026-09-08; a field report, 2026-09-05 | NSIS paths carried a forward slash, so `vendor/fonts` installed as `vendorfonts` | **Fixed** in 0.1.8: every path written into a script is normalized once |
| Void Core, 2026-09-03 | a release broadcast should be computed, not written by hand | **Built**: `mago notify`, the `release` block, [broadcast.md](concepts/broadcast.md) |
| Void Maiz, 2026-09-08 | loose wires in the family graph | **Fixed in Maiz**; their correction (the tangents were dead code) stands |
| Void Maiz, 2026-09-21 | `scan` ran a long name into its version column | **Fixed**: the name column is as wide as the widest name |
| Void Maiz, 2026-09-21 | let a manifest declare its artifact names (`.zip`, `.apk`), so `feed --artifacts` hashes them | **Accepted, not built.** Shape: `release.artifacts: {platform: "Name-{version}-….zip"}` |
| Void Hormiga, 2026-10-18 | a stale `mago` should announce itself | **`mago --version`**, built. Printing it in `stage`'s first line and the receipt is next; refusing a stage over a newer receipt is declined until the first is not enough |
| Void Hormiga, 2026-10-18; again 2026-11-02 | doctor should notice a sibling folder other projects build from that has no manifest, and (2026-11-02) one no other machine can clone | **Built** 2026-11-02: a warning naming who builds from the folder (a `requires` sibling, or `../<Folder>` in a top-level `CMakeLists.txt`), and whether it or any required checkout has a git remote. Void Hammer is the live case. Its manifest and its publication are still Hammer's and the author's to decide |
| Void Hormiga, 2026-10-18 | an application whose old installs cannot sync with new ones needs a word for it | **Accepted in principle**: `release.peers.syncs_with`, rendered as one sentence before the behavior changes. Not built |
| Void Jelly, 2026-10-05 | Mago's 2026-10-04 letter asked for `"kind": "library"`, which the schema refuses | **The letter was wrong.** The schema stands; a library is a `runtime`, with a `kind_note` |
| Void Jelly, 2026-10-05 | resolve a seam's name against the defining side's `provides.documents` | **Open.** Worth doing when a second seam disagreement appears; the Core ⇄ Palabra one is resolved |
| Void Squid, 2026-10-09 | (findings only) Latin-OS's manifest does not parse; the `interacts_with` + `requires` note will recur | Latin-OS's `link: "c-abi"` names a seam where a link kind belongs; it is Latin-OS's to change. Doctor saying which side drops its entry is open |
