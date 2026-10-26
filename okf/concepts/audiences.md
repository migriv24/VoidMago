---
okf_version: "0.1"
type: Concept
title: Who Mago is for — three people, and the phone in each one's hand
description: "The decision of 2026-10-25: applications are independent, sandboxed, distributor-certified artifacts, and Mago is a tool for the people who BUILD them, never a runtime dependency of the people who USE them. What the organizer, the fork maintainer and the family contributor each get from Mago (nothing, a little, everything), how phones change it (the store is the updater, by policy), and what a hub would be later."
tags: [status:partial, audience:all, confidence:high]
timestamp: 2026-10-25T00:00:00Z
---

# The decision

The author, 2026-10-25: Void applications rely on each other's libraries by
design, but **they are structured as independent applications**. We are in the
age of sandboxed apps and single-purpose apps on phones, each certified and
verified by a distributor (an app store, a signed installer). An operating
system built for this family, LatinOS, would make a shared runtime natural.
That is not the world the users live in today.

So:

> **No user ever needs Mago.** Every application ships complete: its libraries
> compiled in (or, on a desktop, shipped beside it) and its own updater (on a
> phone, the store). Mago is how the family is BUILT, kept compatible and
> released. It is a dependency of developing Void software, never of running it.

What follows decides what each of three people gets.

# 1. The organizer — uses the application as it is

A volunteer group's organizer, on Hormiga's phone app, working with their
team. Comfortable with a phone, not with a terminal.

**Mago: invisible, and must stay so.** What Mago contributes reaches them only
as artifacts it produced at release time:

| what they touch | where Mago is in it |
|---|---|
| the app from a store or a download page | Mago staged and checked the package |
| "a new version is available" (desktop) | the feed is Mago's format; the app reads it itself |
| the update notes, including "behavior changes" | Mago carries the release block from the manifest to the prompt |
| a store update (phone) | nothing: the store is the updater (below) |

**Reporting a problem** must not require a GitHub account, a terminal or
knowing which library broke. The application produces a report (Hormiga:
`voidhormiga-cli report`, and a "Report a problem" button still to come). It
then offers two doors: the project's issue form, or **copy it to send to
whoever helps you**. That second door matters most: the organizer's help is
usually not us. It is the group's own technical person, or the business that
runs a fork for them (person 2), and a report that only works on GitHub skips
the person they would actually ask.

# 2. The fork maintainer — a private fork for a business or group

They want features of their own. That is allowed: everything is MIT, and a
fork may be private. They still stand on the family's foundation, and they
report what they need fixed more often than anyone.

**Mago: a development tool they choose to use.** What they need, and the command
that answers it:

| need | Mago |
|---|---|
| a working build of their fork, on a new machine | `mago bootstrap <their fork's URL>`: their app, plus the family at upstream releases (or at their own forks of libraries, if their manifest points there) |
| to know what changed upstream since their fork was based | `mago upstream <project>`: their base version against upstream's newest release, and every `behavior_changes` entry in between (**next to build**) |
| to tell upstream exactly what they run | `mago report <project>`: the whole stack's versions and whether it is a fork, plus the right tracker (**next to build**) |
| to ship under their own name | `mago doctor` warns when a manifest whose `repository` is not upstream's still ships upstream's identity (**next to build**) |
| the licences they must ship | `mago notices` (built) |

**Identity is the part that bites.** A fork that ships with upstream's
application id (`org.voidhormiga.app`), feed URL or signing key is not a
fork to the outside world. It is an impostor update. A store rejects it, or
worse, a phone accepts it as an update to the real app and replaces it. So a
fork's manifest must say whose it is, and Mago checks that the identity in it
is not upstream's when the repository is not upstream's. The fields
(`identity.app_id`, `identity.publisher`, `identity.feed`) are the next
manifest change. Until they exist, the warning has nothing to compare.

**Privacy travels with the fork.** Their database is real people too. The
privacy lint and its local terms list (Hormiga's `what-may-be-public.md`) are
theirs to keep, and `CONTRIBUTING.md` says a report sent upstream follows the
same rules as anyone's.

# 3. The family contributor — develops the Void lineup itself

Somebody who works on Void Core, Void Maiz, Hormiga and the rest. They need the
family's vocabulary (runes, glyphs, mantles, holidays, the dispatcher as the
only door) and its habits (OKF first, conformance tests, messages between
projects).

**Mago: required.** It is how they get the family, and how they learn what a
change costs:

| need | Mago |
|---|---|
| every sibling, full history, at versions that work together | `mago bootstrap <project> --full` |
| is the family consistent? | `mago doctor` |
| what does my change to Void Core break, and how badly | `mago blast voidcore --to <version>` |
| the release message every affected project needs | `mago notify voidcore` (`--issue` to deliver it to their trackers, next) |
| which pairs of projects have agreed seams | `mago matrix`, `mago pair` |
| to learn the family before touching it | `mago learn <project>` (**next to build**): FaultSack's study site and book for that project's OKF |

**Training is FaultSack's, and Mago points at it.** FaultSack reads any project's
OKF and code read-only, ranks what matters by centrality, and produces a study
site (flashcards, quizzes, a module map) and a printed book. Its book already
has a `--with-mago` appendix. So the onboarding path for a new contributor is:

1. `mago bootstrap <project> --full`;
2. `mago learn <project>`, which runs FaultSack if it is present and tells you how
   to get it if not;
3. that project's `CONTRIBUTING.md`, then its `okf/index.md`.

Mago does not vendor FaultSack, and nothing depends on it. It is a sibling
application with an `interacts_with` seam, which is what it already declares.

# Phones

Mago does not run on a phone, and no Void application on a phone depends on
anything outside itself.

**The store is the updater, by policy.**
- Google Play's Device and Network Abuse policy forbids an app distributed on
  Play from updating itself by any means other than Play, or from downloading
  executable code from elsewhere.
- Apple's guideline 2.5.2 forbids an app from downloading or executing code
  that changes its functionality.

So the desktop's feed-and-installer update path does not exist on a phone. A
phone build must not carry one (Hormiga's APK reads the feed only to SAY a
version exists). Mago's role ends at the artifact.

What Mago does for a phone build is the same job it does for a desktop one,
earlier in the pipe:

- **Check that it is self-contained.** On a desktop Void Core ships as a shared
  library beside the binary. On a phone there is nothing beside the binary, so
  every requirement must be compiled in (Void Maiz builds Void Core from source
  on Android). `mago doctor` should refuse a phone platform whose plan still
  has a shared-library payload (**next**).
- **Stage the store artifact** (an APK or AAB today, an IPA when iOS exists),
  with its identity, version and notices, from the same manifest.
- **Write the release notes** the store listing shows, from the same release
  block the desktop prompt shows. One account of a release, whichever door a
  person came through.

**Collaboration on phones** is the application's (Hormiga's LAN and radio
membership), never Mago's.

# A hub, later

A desktop "hub" (Unity Hub is the comparison) that installs several Void
applications side by side and keeps them current is plausible for people who
use more than one. It would be one more application, built on Mago's feed and
plan, and the first application whose users DO see Mago. It is not planned
now, for two reasons:
- it is a second thing to install before the thing a person wanted (the
  argument `distribution.md` already makes for "the hub is a file");
- on phones it cannot exist at all under the store rules above.

On LatinOS, where the family would be the platform, a hub is the natural shape.
That is where to build it.

# What is built today, and the order of the rest

Built (2026-10-25 unless older):
- `bootstrap`, `doctor`, `blast`, `notify`, `matrix`, `pair`, `stage`, `feed`,
  `wizard` and `notices`;
- optional requirements;
- `mago report`: the stack as built here, both trackers for a fork, and no paths;
- the `identity`, `fork_of` and `contribute` manifest blocks, with doctor
  checks: a fork wearing upstream's identity is warned, an application that
  ships with no `app_id` gets a note, and a missing guide or policy is warned;
- `mago upstream`: releases since a fork's base, read from upstream's tags;
- `mago learn`: the reading order, then FaultSack if installed;
- Hormiga's Report a problem, desktop and phone, which copies, shares or opens
  the form.

Next:
1. the phone self-containment check (everyone with a phone): it needs a
   per-platform `link`, since Void Core is `shared` on a desktop and compiled
   from source on Android, and the manifest cannot say so yet;
2. `notify --issue` (person 3, and every maintainer not at this machine);
3. publishing Mago itself, after its local history is cleaned (it held
   correspondence quoting a client), so that `bootstrap` reaches strangers;
4. the libraries tagging their releases, so `bootstrap` has versions to choose
   between.
