---
okf_version: "0.1"
type: Concept
title: Blast radius
description: What it actually costs the family when one project changes, and why the answer is almost never "rebuild everything".
tags: [status:implemented, audience:dev, confidence:high]
timestamp: 2026-09-01T00:00:00Z
---

# Blast radius

The question this whole project was founded to answer:

> a problem i foresee is that, if void core is updated, then you will need to
> update, then also, wouldn't almost everything else need to update?

**No — and the manifests already contain the reason.**

# 1. The verdict depends on the binding, not the version

`requires[].link` says how a dependency is bound into its consumer, and that is
what decides the cost. Four verdicts, cheapest first:

| verdict | condition | what the maintainer does |
|---|---|---|
| **untouched** | no edge | nothing |
| **retest** | reached at a *seam* — a transcript, a conformance case, a JSON document — or `link: import` across an additive change | re-run the contract |
| **reship** | `link: shared`, change inside the compatible line | drop in the new file |
| **rebuild** | `link: static` / `source` / `vendored`, or the line broke | build it again |
| **blocked** | the new version violates a floor a consumer declared | fix the constraint before releasing |

## `import`: the consumer with no build output

`source` means *compiled into the consumer's binary*, and it earns a rebuild.
An interpreted consumer has no binary to rebuild, and saying `source` there
produces a verdict about a file that does not exist.

FaultSack is the case that forced the distinction. It declared `link: "source"`
on Void Core, and its own note in the same file said why that was wrong:

> FaultSack never loads libvoidcore and calls no C ABI, so a runtime rebuild
> costs it nothing; what it depends on is the holidays' Python signatures.

Mago answered *"compiles voidcore into its own binary; a changed voidcore is a
changed faultsack"* about a pure-Python project. Under `import`:

- **additive → retest.** Nothing compiles and nothing ships. What binds is the
  API, so the honest cost is running the tests against the new upstream.
- **breaking → rebuild.** Not because anything is rebuilt, but because the
  consumer's own source must be *changed* before it runs at all — which is
  worse than a rebuild, and propagates for the same reason a rebuild does.

The general rule the two halves share: **the verdict follows what the consumer
has to produce again, not what language it is written in.**

# 2. Only `rebuild` propagates

This is the rule that keeps the number small, and it was a bug in the first
implementation before it was a rule.

A **reship** or a **retest** leaves the affected project's own build output
**byte-identical**. Nothing downstream can be affected by a file that did not
change, so the walk stops there. Only `rebuild` and `blocked` produce a new
artifact, and only they push one hop further.

Concretely: Void Core 0.2.12 → 0.2.13 is additive. Void Maiz links Core as a
shared library, so Maiz is a **reship** — and `voidmaiz.lib`, the static library
Void Hormiga compiles in, is unchanged. Hormiga therefore inherits *nothing*
through Maiz. It gets its own **reship** verdict from its own direct edge to
Core, and ships one new DLL.

Without the rule, the same change reported a **rebuild** of Hormiga through the
two-hop path — which is exactly the false alarm that makes a maintainer stop
trusting the tool.

# 3. Below 1.0, the minor position is the break

Void Core states its own contract in its manifest: *"The C ABI is additive-only
within a 0.2.x line."* So `0.2.7 → 0.2.12` is compatible and `0.2.x → 0.3.0` is
not. Mago infers `compatible` or `breaking` from the version move on that rule,
and `--change` overrides it when the version number lies — which it eventually
will, because a version number is a promise and not a proof.

# 4. A seam is symmetric, and only one side usually writes it down

Void Core's manifest records Void Hormiga under `interacts_with`; Hormiga's
records a hard `requires` on Core. That is **one edge described twice**, and
counting it as both would report a rebuild *and* a retest for the same
relationship. The registry filters any pair that also has a `requires` edge, in
either direction, and treats the remaining seams as symmetric: either end moving
is a reason to re-run the contract, regardless of which repository wrote it.

# 5. A partner with no manifest is *unknown*, not *untouched*

Void Core's 0.2.14 broadcast was computed by hand, and it said so about its own
verdict table:

> The verdict table above is a guess about repositories nobody re-read. […]
> They got their verdicts from Void Core's own `interacts_with` notes — which
> are *our* description of *their* binding, written when the manifest was, and
> never checked against theirs since. We have no idea whether Reyna still emits
> transcripts in the shape we claim.

A project Mago cannot read cannot be given a verdict, and leaving it off the
page is worse than useless: **an absent row reads as "unaffected".** So `blast`
prints an `UNVERIFIED` section naming every partner the changed project claims
and Mago has never seen, and `doctor` says the same thing standing still. On
the real family that is exactly Void Reyna, Void Loops Studio and Void Unity —
the three repositories the hand-written table guessed about.

# 6. A seam belongs to whoever defines the document

`interacts_with` edges are symmetric, with two exceptions, and the second one
took a while to see.

A **wildcard** edge (`name: "*"`) is a standing habit rather than a contract
with anybody: Mago reads every project's `void.json`, and nothing is downstream
of the reading.

A **named** edge at a seam is a real contract — but only the project that
*publishes the document* can move it. `void.json` is Mago's format, declared in
Mago's own `provides.documents`. So a `void.json` seam edge is a retest when
**Mago** changes, and nothing at all when anybody else does. Without that rule
the family gets one of two wrong answers: either a Void Core patch reports a
retest for Void Mago, which reads a JSON file and cannot be affected by
anything in it; or FaultSack — which parses the schema Mago defines — never
hears that its input format moved.

# 7. What this cannot tell you

Mago reports what the manifests **declare**, not what the code **does**. A
`link: shared` dependency whose ABI silently changed inside its compatible line
will be reported as a reship and will crash anyway.

Closing that gap needs a build farm that actually compiles the combinations and
runs the conformance suites — the difference between a compatibility matrix
that is *declared* and one that is *true*. That is not built, and until it is,
every verdict here is a claim about the manifests rather than about the
binaries. Saying so is cheaper than being trusted wrongly.

See also [[distribution]], and Void Hormiga's application-boundaries page for
where the `runtime` / `capability` / `application` vocabulary comes from.
