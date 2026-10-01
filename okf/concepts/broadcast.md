---
okf_version: "0.1"
type: Concept
title: The broadcast
description: What a release has to tell the rest of the family, which four fifths of are arithmetic — and the fifth Mago refuses to invent.
tags: [status:implemented, audience:dev, confidence:high]
timestamp: 2026-09-03T00:00:00Z
---

# The broadcast

`mago blast` answers *who is affected, and how badly*. That answer is
mechanical, from the manifests, and it is **not a release message**.

We know it is not, because one was written without it. Void Core shipped 0.2.14
by reading eight sibling repositories with human eyes and writing eight messages
into eight repository roots, each with a hand-computed verdict table at the
bottom in the shape of the one Mago already prints. Then it wrote down what a
verdict alone had failed to carry — which is the closest thing this project has
to a specification, so it is quoted rather than paraphrased:

> 1. **The verdict.** Mechanical, from the manifests. Cheapest and most valuable.
> 2. **Behavior changes that are not ABI changes.** […] **This is the category a
>    dependency resolver structurally cannot see**, and it is the one that bites.
> 3. **New optional surface.** Nobody must adopt them; anybody might want to. A
>    resolver has nothing to say here, but a *notifier* does.
> 4. **Per-project relevance.** […] **None of that is derivable from a
>    manifest**, and it was the most valuable part of every message.
> 5. **Where to read the rest.** Mechanical, if a manifest names its contract
>    documents.

`mago notify <project>` produces 1, 2, 3 and 5, and leaves 4 as a labelled hole.

# 1. The one fact a resolver cannot derive

Every verdict in that hand-written broadcast was derivable from the manifests
*plus one thing a person knew*: that the change was additive within the 0.2.x
line. Core's own message named the fix:

> Nothing here needed judgement except that one bit, and that one bit is a
> field a release could declare.

So a manifest may now carry a `release` block, and it is the only part of a
manifest that describes an **event** rather than a standing fact:

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

With it, `mago blast voidcore` and `mago notify voidcore` need no flags at all:
the version move and the change kind come from the release, and Mago says which
of the two it used — *(declared by the release block)* — because a verdict is
only as good as the fact underneath it.

# 2. Behavior changes are the reason this exists

`link --weight` now refuses a non-number. No signature changed. No version
constraint is violated. Every verdict Mago computes about that release stays
correct, and any host that depended on the old silent `0.0` breaks anyway.

**A resolver cannot see that category at all.** It is not in the version, not
in the ABI, not in the link kind. The only way it reaches a downstream is if
somebody writes it down and something carries it. That is the whole argument
for `notify` being a separate verb rather than a longer `blast`.

# 3. The hole, on purpose

Section 4 of every generated message — *what this release means for you
specifically* — is written as an HTML comment addressed to whoever is holding
the release, and the words `_Not yet written._`

Core's message is right that this part does not scale, and it is right that it
was the most valuable part of all eight. Both of those being true is exactly
why Mago must not fill it: **a generated paragraph in the place of the one
section a person was supposed to write is worse than a blank, because a blank
is visible.**

# 4. Sending

```
mago notify voidcore                 # print the whole broadcast
mago notify voidcore --json          # the same, for a script
mago notify voidcore --write         # one message into each project's root
mago notify voidcore --all           # include the untouched: "nothing to do"
```

`--write` is never the default, because it is the one thing here with a side
effect in somebody else's repository. Filenames follow the convention the
family already uses by hand:
`MESSAGE_FOR_<RECIPIENT>_<topic>-<date>.md`.

Paths inside a message are rewritten relative to its **recipient**, so
`SPEC.md` reaches Void Hormiga as `../VoidCore/SPEC.md` — a message that tells
a reader to open a file they do not have is a message that gets ignored.

See also [[blast-radius]] for the verdicts themselves.
