---
okf_version: "0.1"
type: Concept
title: Iscariot — the family's check that no repository betrays the people it served
description: "Every Void repository is public, and most ship their okf/ inside every installer. Iscariot scans the whole family for what must never be published (credentials, agents' correspondence, private names, personal data), ranks it by tier, and keeps a local database of flags that each project's agent answers with `fixed` or `override`. It repairs nothing but a .gitignore. Why it exists, what it checks, where its state lives, and the rules it enforces."
tags: [status:current, audience:all, confidence:high]
timestamp: 2026-10-30T00:00:00Z
---

# Why it exists

Void Hormiga published private material twice, and both times the rule against
it already existed in prose. Its answer was a mechanism: `tools/lint_public.py`,
a gitignored `.private-terms` list, a pre-commit hook. See Hormiga's
`okf/concepts/foundation/what-may-be-public.md`, which this page assumes.

That mechanism covers one repository. The family is twenty, every one of them
public. Agents write in each other's folders, and an agent's offhand note
("something learned from a client's database is...") lands in whichever OKF it
happened to be working in. So the check has to run across the family, and it has
to remember what it found. Otherwise a flag raised today is forgotten by the
next session.

The name: an application that publishes what a client trusted it with has
betrayed that client.

# What it checks, most urgent first

| tier | name | what | who resolves it |
|---|---|---|---|
| 0 | credential | token and key shapes (GitHub, AWS, Slack, OpenAI, Anthropic, Google, Stripe, private-key blocks); a high-entropy value assigned to a key-like name | the project's agent. **Revoke it too**: history keeps it |
| 1 | secret file | `.env`, `*.pem`, token files and the like, tracked or not ignored; a `.gitignore` missing the family's secret lines | `iscariot ignore --apply`, then the agent |
| 2 | local only | `MESSAGE_FOR_*` / `REPORT_FROM_*`, `okf/reports/`, databases, `.private-terms`, an agent's local settings | the agent, or `ignore --apply` |
| 3 | private term | a line naming anything on the private-terms lists; a commit author identity that names one | prose: anyone; code: the agent |
| 4 | personal | an email outside the example domains, a phone number outside 555, a home directory | prose: anyone; code: the agent |

**What gets scanned is what a push would publish:** tracked files, plus untracked
files that are not ignored, because the next `git add -A` takes them. A folder
that is not a repository yet is scanned whole (minus build output). Third-party
`vendor/` trees are skipped. `--history` also reads every line ever added in every
commit (tiers 0, 3, 4) and every path ever committed (tiers 1, 2). That is the scan
to run before a repository's **first** push, because a first push publishes all of
it.

# Where its state lives, and why none of it is in a repository

| what | where |
|---|---|
| the family's private terms | `MAGO_HOME/private-terms` (default `~/.mago/`), beside `roots.json` |
| a project's own terms | that project's gitignored `.private-terms`; both lists are merged |
| the flags | `MAGO_HOME/iscariot.db`, SQLite |

The terms list cannot be committed, because the list is itself the secret. The
database holds paths, line numbers, rules and a hash of each line. **It never
stores or prints the matched text**: a report that echoed a phone number would
publish it a second time. Same format as Hormiga's list: one term per line,
matched ignoring case; `re:` starts a regular expression; `#` starts a comment.
**Add a line the day a new client, organization, person or private project
starts using any Void application.**

# The life of a flag

```
open ──fixed──▶ claimed ──next scan, gone──▶ fixed
  │                └──next scan, still there──▶ open (reopened)
  ├──override──▶ overridden   (never raised again)
  └──next scan, gone, nobody claimed it──▶ gone
```

A flag is answered by **the agent responsible for the project**, because a
hard-coded credential or a private name inside logic needs someone who knows what
depends on it:

```
python ../VoidMago/iscariot/iscariot.py flags voidsquid
python ../VoidMago/iscariot/iscariot.py fixed 412 --by voidsquid --note "read from the environment"
python ../VoidMago/iscariot/iscariot.py override 413 --by voidsquid --note "a fixture value, not a key"
python ../VoidMago/iscariot/iscariot.py scan voidsquid     # verifies every claimed fix
```

A claimed fix is **verified**: the next scan reopens it if the finding is still
there. An override is **accepted as true**. That is deliberate for now: an agent
that says "you flagged something wrong" knows its code better than a regular
expression does, and the override is recorded with its reason and author.

`iscariot notify <project> --write` puts the open flags into the project's root as
a `MESSAGE_FOR_<PROJECT>_iscariot-open-flags-<date>.md`, the family's ordinary
channel. It refuses when that repository would publish the message.

# The rules it enforces

1. **Correspondence is local.** `MESSAGE_FOR_*` and `REPORT_FROM_*` are gitignored
   in every Void repository. A receiving session folds the *finding* into its
   OKF, written as a fact about the project with any client unnamed, and deletes
   or archives the message. Until 2026-10-30 Void Mago committed its own letters
   into recipients' repositories ("Inbox: ..."); that was wrong, and those commits
   were removed from every history that had not been pushed.
2. **No machine's paths in prose.** A sibling is `../VoidCore`, never an
   absolute path through somebody's home folder.
3. **The commit identity is public.** Every commit publishes its author email. The
   family commits as the GitHub no-reply address.
4. **Iscariot repairs one thing, the `.gitignore`.** It is hygiene, not logic.
   Everything else is reported, and the agent decides.

# What it cannot do

It knows shapes and the names it is given. It cannot know that a sentence
describes somebody: "an organization serving people who can be harmed by being
found" contains no term at all. **Before committing, read the diff as a
stranger would.**

And it cannot reach what is already out. Removing a file does not remove it from
history, from a clone, or from a release asset built while it existed. When
something private was published, three separate things are owed (the tree, the
history, the releases) and the author decides each one. Hormiga's page lists
them.
