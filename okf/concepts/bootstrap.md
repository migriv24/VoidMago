---
okf_version: "0.1"
type: Concept
title: Bootstrap — the family onto a disk that has nothing
description: "`mago bootstrap`: clone a project and everything it requires as siblings, at the newest release every requirer accepts, without touching anything already on disk. The first answer to §8.1 (the sibling layout does not survive a second developer), built 2026-10-24 for Void Hormiga's contributors. Includes the version rule as an interval intersection, and what the family must do for it to mean anything (tag releases)."
tags: [status:partial, audience:dev, confidence:high]
timestamp: 2026-10-24T00:00:00Z
---

# Why

§8.1 of the [index](../index.md): `mago` resolves against whatever is on
disk, which "works for one developer and does not survive a second one." The
second developer arrived on 2026-10-24, when Void Hormiga opened to outside
contributions (its `okf/concepts/contributing/`). Before this, a contributor
had to copy four `git clone` lines out of a README. Hormiga's CI cloned each
dependency's `main`, which no compatibility check had ever approved.

    mago bootstrap https://github.com/<owner>/VoidHormiga --into ~/Void
    mago bootstrap ./VoidHormiga              # a checkout you already have
    mago bootstrap ./VoidHormiga --dry-run    # decide, clone nothing

# What it does

1. Read the target's `requires`.
2. For each dependency, find its address. **The edge carries it**: a
   `requires[]` entry may name a `repository`. The dependency's own manifest
   names it too, but that manifest is inside the repository nobody has cloned
   yet.
3. Choose a release tag (below), and clone it at the `sibling` path the
   requirer names, at depth 1. The target itself is never shallow, because it
   is the repository the contributor works in.
4. Read the clone's manifest, hold its version to the ranges, and continue with
   its `requires`. Each project is laid out once, however many projects require
   it.

**It never touches what is already there.** A folder at a sibling path belongs
to the person. If it has a manifest, its version is read and judged against the
ranges. If it has none (Void Hammer has no `void.json` yet), it is used as it
is, with its version unchecked. A folder that is too old is reported with the
fix, and is never checked out, pulled or deleted.

**Optional dependencies** (`"optional": true`) are reported as absent when
they have no repository or no remote. They are never a failure. `plan` and
`doctor` treat them the same way: Hormiga builds without Void Hammer, and a
registry without it is a note, not an error.

# The version rule

For one project p, each requirer contributes a range, and the set of ranges is
R_p. The admissible releases are

    A_p = { t ∈ tags(p) : ∀ r ∈ R_p, r allows t }

and the chosen release is max A_p in the version order. The family writes four
range forms: `>=` floors, `^` lines, `~` minors, and `*`. Each is an interval on
that total order, so A_p is the tags inside the intersection of intervals, which
is itself an interval. "The greatest admissible tag" is therefore well defined
whenever any tag is admissible, and it is the newest release nobody objects
to.

The walk discovers requirers as it goes, so R_p can grow after p was cloned.
A new range can only shrink the intersection, so the release already chosen
either still lies inside it or it does not. If it does not, the step says so
and the run fails, and nothing is moved. Moving it would invalidate a choice
some other project was already laid out against. The honest answer is to
show the conflict, as `plan` does with an unsatisfiable install.

A default branch (no tags) is taken on trust, and the clone's manifest version
is then held to the ranges, so a branch that has moved past a requirer is
caught.

# What the family has to do for this to mean anything

**Tag releases.** Measured on 2026-10-24 from an empty folder: Void Hormiga
has sixteen release tags, and Void Core, Void Maiz, Void Allomone and Void
Palabra have none. So every library came from its default branch, and the
note says plainly that no matrix approved it. The rule above is built and
tested. It has nothing to choose from until the libraries tag the versions their
manifests already declare. `mago notify` is the natural place to remind a
release to tag itself.

**Measured again on 2026-11-02, with Void Rancho as the fifth pillar.** Still
no library carries a tag, and a tag cannot simply be added now, because most
libraries' code has moved past the version they declare:

| library | declares | since that version was set |
|---|---|---|
| Void Core | 0.2.15 | prose and CI only. **Tag `v0.2.15` at `main`.** |
| Void Rancho | 0.1.0 | first publication. **Tag `v0.1.0` when it is pushed.** |
| Void Maiz | 0.1.0 (2026-09-01) | graph analysis, portable and iOS apps, `maiz::RanchoLink` |
| Void Palabra | 0.1.0 (2026-09-01) | sync protocol, replicas, Reticulum |
| Void Allomone | 0.1.0 (2026-09-01) | additions to its public headers |
| Void Jelly | 0.1.0 (2026-10-05) | about 9 900 lines, cgltf vendored |

Tagging the last four at the commit that set `0.1.0` would be accurate and
harmful: bootstrap would hand every requirer (all of them `*`) the September
code, not the code they have been built and tested against (Rancho's
superproject build needs Maiz's `voidmaiz_graph` target, added 2026-11-02).
Each of the four needs a version bump with a `release` block first, and that
is its own agent's decision. Each was told on 2026-11-02.

**Name repositories on the edges.** Void Hormiga's `requires` carry them as of
2026-10-24. A required dependency that names none fails with the field to add.
On 2026-11-02 only Hormiga and Rancho (named while publishing it) did. The
edges of FaultSack, Interaction Combinators, Void Chisme, Void Jelly, Void
Loops Studio, Void Maiz, Void Squid and Void Verguenza carry none, so none of
those can be bootstrapped yet. Each was told.

# Measured

From a folder holding only Void Hormiga's `void.json`, against the public
repositories: four libraries cloned, Void Hammer reported absent and optional,
in 7.6 s, 23 MB on disk. `tests/bootstrap_smoke.cpp` covers six cases against a
fake git with no network:

- the version rule;
- a three-project family laid out once each;
- a stale checkout left alone and reported;
- a hand-made checkout used as it is;
- a dry run;
- a required dependency with no address.

# Not yet

- **Mago itself is not published.** Its manifest names a GitHub repository that
  does not exist, so a contributor cannot run this yet. Before publishing, the
  correspondence files Mago had committed were untracked (they quote a client),
  and the local history still holds them.
- **Hormiga's CI still clones by hand.** It can call `mago bootstrap` once Mago
  is public.
- **No build step.** It prints where each project landed, not how to build it,
  because manifests do not yet say how. A `build` block is the obvious next
  field.
