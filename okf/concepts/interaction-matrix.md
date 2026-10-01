---
okf_version: "0.1"
type: Concept
title: The interaction matrix
description: Every pair of Void projects in one table — why it is symmetric, where the one asymmetry goes, and why a cell is a join over evidence rather than a value somebody typed.
tags: [status:implemented, audience:dev, confidence:high]
timestamp: 2026-09-04T00:00:00Z
---

# The interaction matrix

The ask, and the observation that designed it:

> i would really like a nice interface to tell me if there are conflicts across
> any of the void based applications

> it will be very symmetrical cuz like the interaction between void allomone and
> void maiz is the same as the interaction between void maiz and void allomone
> (like AB = BA type of logic here)

That is correct, and it is worth taking literally rather than as a hint about
layout. `mago matrix` is the table; this page is what is underneath it.

# 1. Symmetric, except for the part that is not

The relationship between two projects is symmetric. The **direction** is not:
Void Maiz requires Void Core, and Void Core does not require Void Maiz. Both
are facts about the same cell.

Every square matrix decomposes uniquely into a symmetric and an antisymmetric
part:

```
M = S + A        S[i][j] =  S[j][i]        (the relationship)
                 A[i][j] = -A[j][i]        (which way it points)
                 A[i][i] =  0              (nothing points at itself)
```

So the relationship is **S** — one `Pairing` record per unordered pair, stored
once — and the direction is **A**, a single integer that is negated when you
read the cell from the other side. In code that is one line:

```cpp
cell.direction = (i < j) ? upper_[k].direction : -upper_[k].direction;
```

`AB = BA` then holds exactly, and the asymmetry the family really has is
preserved instead of being flattened into it.

**Storage follows.** Only the strict upper triangle exists: `N(N-1)/2` records
rather than `N²`. Seven projects is **21 pairs, not 49 cells** — and 21 is the
number a person actually has to author. The index is

```
k(i,j) = i·N − i(i+1)/2 + (j − i − 1),    i < j
```

# 2. A cell is a join, not a value

Two projects can be related in more than one way at once. Void Core both has a
`requires` relationship with Void Hormiga *and* names it under
`interacts_with` — one edge described twice, which `mago doctor` reports
because two descriptions of one thing drift apart. A cell therefore has to
**combine evidence** rather than hold whichever fact was read last.

Evidence is joined on the product of two chains:

| chain | order |
|---|---|
| **bond** — how tightly they touch | `none < authored < seam < requires` |
| **cost** — what an additive change costs | `untouched < retest < reship < rebuild < blocked` |

Each is a total order, so each is a lattice; ordered componentwise, their
product is a lattice. A cell's value is the **least upper bound of every piece
of evidence about that pair** — the tightest bond anybody declared, and the
worst cost it implies.

This is where the symmetry stops being a convention somebody has to remember:

> The evidence for `{a,b}` is gathered from **both** manifests before it is
> joined, and join is commutative, associative and idempotent. So the result
> cannot depend on which of the two projects you asked about first.
> **Symmetry is a theorem about the computation, not a rule imposed on it.**

The alternative — filling in two triangles and trusting them to agree — is the
same mistake as two manifests describing one seam differently, which is the
contradiction this whole table exists to surface.

The cost chain is the same one `mago blast` walks, read from one function
(`cost_of_link`) so there is exactly one answer to "what does a shared link
cost". A cell reports the **pairwise** cost only; `blast` answers the
transitive question, and a table of edges that tried to report paths would be
answering a different question in a smaller space.

# 3. The diagonal

`M[i][i]` is not an interaction — a project has no seam with itself. In a
multiplication table the diagonal holds the squares; here it holds **the
project**: kind, version, status, its degree in the table, and whatever a
person authored about it. It reads as the legend, down the middle, which is
where a reader's eye already goes.

# 4. The empty cells are the result

```
9 of 21 pairs have no edge at all, which is the number this table exists to
keep large.
```

A family whose matrix fills in has stopped being a family of separable
projects. Density is the metric worth watching, and it is the same claim the
[blast radius](blast-radius.md) makes from the other direction: a Core patch
costs one DLL because most pairs are empty.

# 5. The authored half: `family.json`

Three of the twelve bonded pairs in the real family carry a fact no manifest
holds, and one carries a contradiction:

```
voidcore -- voidpalabra   seam   retest   seam: c-abi   DISPUTED
```

Void Core calls that seam `c-abi`; Void Palabra calls it
`state document + command journal`. Both manifests are internally consistent
and honestly authored. Neither is wrong from inside its own repository —
**because a fact about a pair belongs to neither repository.**

`family.json` is where it goes: one document, held by the project whose job is
the family, carrying `note`, the agreed `seam`, what the two `shares`, and a
`status`. Nothing in it is computed, and Mago never writes a sentence into it
on its own — `mago pair a b --note "..."` is a person talking.

Its effect on the matrix:

- an authored note on a pair no manifest connects gives the cell the
  `authored` bond, which is the only way that relationship can exist at all;
- an authored `seam` **settles** a disputed one — the cell stops being
  DISPUTED, and `mago doctor` still calls the manifests wrong (they are) but
  now carries the answer: *"family.json settles it as X, so the fix is known."*

It is shaped as a **graph** — projects and pairs — rather than as rows and
columns, because the same content has to become a table, a node canvas, and
eventually a Void Core mantle that Void Palabra can merge across two machines.
A table is one projection of a graph; a graph is not a projection of a table.
See [desktop](desktop.md) §4.

# 6. What it does not do

It reports **declared** relationships, exactly as [[blast-radius]] reports
declared verdicts. Two projects that share an asset folder nobody wrote down
are an empty cell until a person fills it in — which is the argument for the
authoring surface being one keystroke away from the table, and the reason
Void Mago Desktop exists at all.
