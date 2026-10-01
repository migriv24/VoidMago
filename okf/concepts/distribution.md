---
okf_version: "0.1"
type: Concept
title: Distribution, signing and trust
description: What it actually takes to hand a Windows user a Void application they can run without a scary warning, and what the prior art already settled.
tags: [status:research, audience:dev, confidence:medium]
timestamp: 2026-09-01T00:00:00Z
---

# Distribution, signing and trust

Research notes for §8.3 and §8.6 of [the index](../index.md). Nothing here is
built yet; the point is to stop the family from paying for the wrong thing.

# 1. An open-source licence does not stop SmartScreen

The founding assumption was:

> in order for computers to not think they are downloading a virus, all void
> based applications will have to register with some open source license.

**This is not how it works, and the correction is worth money.** MIT in the
repository is orthogonal to what Windows does when someone double-clicks a
downloaded `.exe`. There is no registry of open-source projects that Windows
consults. What SmartScreen weighs is:

1. **A code signature** tying the file to a verified publisher identity, and
2. **Reputation accumulated by that identity** across downloads over time.

An unsigned binary starts with no reputation and no identity to accumulate any,
so every release begins from zero. A *signed* binary accumulates reputation
against the publisher, so later releases inherit the trust the earlier ones
earned. That is the whole mechanism, and it is why signing matters much earlier
than it feels like it should: the reputation clock only starts once there is an
identity to attach it to.

Two further corrections:

- **EV certificates no longer buy instant SmartScreen trust.** They used to.
  They do not now, and paying the EV premium for that reason alone is wasted.
- **Azure Artifact Signing (formerly Trusted Signing)** is Microsoft's
  recommended route for developers distributing outside the Store. The private
  key lives in an Azure HSM and is used through a managed API, so there is no
  USB token to keep plugged into a build machine, and it wires into GitHub
  Actions directly. **Certum** sells code-signing certificates on an
  open-source-developer discount and its CA is present on every Windows
  install, which is the cheaper path if a monthly Azure line item is unwelcome.

The practical consequence for sequencing: **one signing identity for the whole
family, established early, used for every release of every application.** Twelve
applications each accumulating reputation separately is twelve slow starts;
one publisher identity signing twelve applications is one.

# 2. Do not write the update client

[TUF — The Update Framework](https://theupdateframework.io/) is a CNCF
graduated specification for exactly this problem, and its premise is the one
worth adopting: **assume the repository will eventually be compromised, and ask
what a client can still guarantee afterwards.** Its answer is four separate
signing roles (`root`, `targets`, `snapshot`, `timestamp`), metadata that
expires, and signature thresholds — arranged so that stealing any one key is
not enough to push a malicious update.

The family already writes `"signed": "ed25519"` in every manifest, which is a
key type, not a trust model. TUF is the trust model, and the part it answers
that ed25519 alone does not is **key rotation** — the open question in §8.3.

The maintained implementations are Python, Go and Rust, and **none of them is
reachable from a C++ binary that links nothing** — which is what `mago` is, on
purpose. So the choice is not "adopt python-tuf"; it is one of:

- **Implement the client half against the spec.** This is the smaller half:
  verifying signed, expiring metadata with a signature threshold, which is
  ~600 lines over an ed25519 primitive. Void Hormiga already vendors
  **libsodium**, so the primitive is in the family and vetted.
- **Keep the repository side in whatever language is convenient**, since it
  runs on a build machine and not on a user's, and only the client is
  constrained.

That split is worth naming now, because it means the "no dependencies" rule
does not have to be broken to get TUF's guarantees — only the client workflow
has to be written rather than imported.

# 3. Do not write the installer engine either

[Velopack](https://github.com/velopack/velopack) is the closest existing thing
to the installer-and-updater half of Mago: Rust, cross-platform, **binary delta
updates** so a user downloads only what changed, language-agnostic bindings, and
it replaces Squirrel on Windows and Sparkle on macOS. Two caveats: it is
pre-1.0, and its Linux support is fragmented across DEB/RPM/AppImage/Flatpak in
the way Linux packaging always is.

This is why Mago is a **compiler and not an engine**. The emitter target is a
detail; the registry is not. NSIS is the first emitter because an `.nsi` file is
plain text and costs nothing to try. Velopack is the second, and the one that
brings delta updates — which matter as soon as `libvoidcore.dll` is being
reshipped to every application on every patch.

# 4. Dependency solving is NP-complete, and the family is nowhere near needing that

The EDOS work (2005) first showed the package installation problem is
NP-complete and gave a 3SAT encoding for Debian and RPM. Later systems answered
it seriously: Spack moved to **Answer Set Programming** with `clingo`, because
"which versions are compatible" is only part of its problem — it also reasons
about compilers, build variants, GPU backends and microarchitectures. Nix and
Guix take the other route, hashing the entire build closure.

**Mago needs none of this yet, and should not pretend to.** Six projects, one
version each, edges that form a small DAG: a depth-first walk with constraint
checking is not merely adequate, it is the correct amount of machinery. What
the research changes is the *manifest format*, not the resolver — `requires[]`
already records name, range, link kind and platform separately, which is the
shape a real solver would want if one is ever needed. The trigger for revisiting
this is not project count; it is the first time two Void projects need
**different versions of the same third project simultaneously**. Nothing in the
family does today.

# 5. Hosting, and the shape of the eventual website

The author's plan is a suite site — `voidsuite.com` or similar — hosting the
downloads, with Void Hormiga behind it as the database. Two things the prior art
suggests:

- **The Blender model**: the project's own site is canonical, stores are
  secondary, and old versions stay downloadable forever. Side-by-side installs
  are already true in every Void manifest, so the site should not contradict
  them by offering only "latest".
- **Releases are static files.** A signed manifest plus content-addressed
  artifacts needs a bucket and a CDN, not an application server. That keeps
  AWS — and the Antfarm's involvement — a later question rather than a
  precondition, and it is what makes the download side survivable by one person.

There is **no open-source equivalent of Adobe's Creative Cloud desktop app** to
copy. The closest commercial patterns are suite installers with per-application
components and a cloud-driven manifest — which is the shape `mago wizard`
already emits, one selectable component per application, runtimes folded in.

See also [[blast-radius]].
