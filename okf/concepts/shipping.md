---
okf_version: "0.1"
type: Concept
title: Shipping an application
description: From a checkout to somebody else's machine — the staging step that was missing, why the hub is a file rather than an application, and the split between what Mago emits and what an application implements.
tags: [status:partial, audience:dev, confidence:high]
timestamp: 2026-09-04T00:00:00Z
---

# Shipping an application

The question, from the author:

> i just wanna start testing void hormiga on other devices already. But i also
> wanna be able to update void hormiga, and have those other devices receive
> updates somehow.

Two different problems that look like one. Getting it there is a build
problem; keeping it current is a documents-and-consent problem. Mago's share of
each is different, and neither is "Mago installs software" — **Mago compiles
`void.json` into an installer**, and everything below is what that sentence
turns out to require.

# 1. The five steps, and which are Mago's

```
mago plan   voidhormiga --platform windows-x64   what belongs in the package
mago stage  voidhormiga --platform windows-x64   put those files in one folder
makensis    stage/…/voidhormiga.nsi              compile the installer
mago feed   voidhormiga --base-url …             say what exists
<upload>                                          a static file host
```

Steps 1, 2 and 4 are Mago's and are built. Step 3 is NSIS's, and always was —
staging, elevation, atomic swap and uninstall entries are solved problems with
maintained implementations, and the part nobody else can compute is *which
files belong in the package and what breaks when one of them moves*. Step 5 is
a bucket.

## The step that was missing

`mago plan` had always known which files belong in the package, and
`mago wizard` had always written a script referencing them — at paths under a
`--source-root` **that nothing had ever produced.** The script compiled against
a directory that did not exist. A working answer to the hard question and no
answer at all to the easy one.

`mago stage` is the easy one, done: it walks each project's checkout, finds the
file the manifest promised, and copies it into the layout the emitter already
expects. Its value is not the copying.

# 2. Three names for one file

Conflating any two of these is a bug that only appears on somebody else's
machine:

| | example | who uses it |
|---|---|---|
| the **build output** | `libvoidcore.dll` | the compiler wrote it |
| the **release asset** | `libvoidcore-win-x64.dll` | a manifest declares it, because a release page holds every platform at once |
| the **installed file** | `libvoidcore.dll` | what the consumer's binary asks the loader for |

The platform tag exists to keep three builds apart in one folder on a download
page. **It must not survive onto the machine** — an executable linked against
`libvoidcore.dll` does not start next to a file called
`libvoidcore-win-x64.dll`. `installed_name()` is that rule, read by both the
stager and the emitter so the two halves cannot drift; the NSIS test that used
to assert the tagged name was asserting a bug.

## Off Windows, the "installer" is an archive (2026-09-18, 0.1.8)

`wizard` emits NSIS and nothing else, so on every other platform the file a
person downloads is the tarball the application's own release runner packs:
`VoidHormiga-0.1.4-linux-x64.tar.gz`, stable copy
`VoidHormiga-linux-x64.tar.gz`. Until 0.1.8, `installer_name()` answered
`VoidHormiga-0.1.3-linux-x64-setup` for Linux, a file nobody had ever built. So
`feed --artifacts` had nothing to hash, it wrote `"sha256": null`, and Void
Hormiga's Linux client refused the update for want of a digest. The client was
right to refuse. The name was wrong. The rule now names the file that exists,
and `ship_smoke` asserts it for Linux and macOS as well as Windows.

The order this imposes on a release: the runner's archives must be **on disk in
`--artifacts` before `mago feed` runs**, or the feed hashes nothing for those
platforms, the same as before.

## A fourth name, for the installer

The same distinction, one level up. `installer_name()` puts the version in:
`VoidHormiga-0.1.0-windows-x64-setup.exe`. An **updater** wants exactly that —
it is asking for a specific release and the feed told it which.

A **download page** wants the opposite. A URL with a version in it goes stale
the moment there is a newer one, so a website's button either 404s or points at
last month's build until somebody remembers to redeploy the site. The fix is
the same bytes attached a second time under a name with no version in it —
`stable_installer_name()`, `VoidHormiga-windows-x64-setup.exe` — which
`/releases/latest/download/` then resolves forever.

**Shipping a version must never mean redeploying a website**, and every Void
application will have a download page, which is what makes this family
vocabulary rather than one project's convention.

Mago **names** it and **checks** it; a person still attaches it. `feed` writes
`stable_file` beside `file` so the name is a stated fact rather than prose in
one repository, and `feed --artifacts` looks for that copy and hashes it. The
check is the point: this is the only unversioned manual step in an otherwise
generated pipeline, and its failure mode is the worst available — updates keep
working perfectly, because they follow `file`, while the button 404s for
everyone who is not already a user. A *stale* copy is worse still, being
present and plausible, so the hash is compared rather than the name merely
being found.

# 3. What staging is actually for

Two checks, and both found something real on the first run against Void
Hormiga.

**A manifest that promises a file nobody built.** Hormiga's `void.json`
declares `ships_beside_binary: ["AGENT-GUIDE.md", "okf/", "fonts/"]`, and there
is no `fonts/` in the repository. Staging fails rather than producing a package
with a hole, because a hole is discovered by a person on another machine when
the application does not open.

**A binary that needs a file nobody mentioned.** This is the better one.
`voidhormiga.exe` imports `libwinpthread-1.dll` — a MinGW runtime that lives in
the developer's toolchain directory and on no other machine in the world. No
manifest mentions it, because no manifest *could*: it is a fact about how the
binary was linked, not about what the project declares.

So `mago stage` reads the PE import directory of everything it stages and
reports any DLL that is neither in the package nor provided by Windows:

```
WILL NOT START -- imported by a staged binary, not in the package,
                 and not provided by Windows
  voidhormiga.exe           libwinpthread-1.dll
  voidhormiga-cli.exe       libwinpthread-1.dll
```

That is exactly the founding question — *it passes its own tests, but what
breaks somewhere else* — asked of a binary instead of a manifest, and it is
worth the hundred lines of PE parser it costs. **The fix belongs in the build**:
Void Mago's own `CMakeLists.txt` already does
`target_link_options(voidmago PUBLIC -static)` under MinGW for precisely this
reason, and the comment beside it says why.

## The check staging cannot do

Staging inspects a tree. It cannot see a bug in the script that *reproduces*
the tree, and there was one.

`ships_beside_binary` entries are written in the portable form, with forward
slashes, and that is correct for a manifest. `fs::path` forgives a forward
slash on Windows, so `mago stage` built a real nested `vendor/fonts/` and
reported it truthfully. **NSIS is not a filesystem API**: `SetOutPath` reads
that slash as an ordinary character in a directory *name*, so the fonts
installed into a folder called `vendorfonts` — while `File /r` accepted the
same string as a separator and copied every byte into it.

The two directives disagreeing is what made it quiet. Not "installer failed"
and not "files missing", but an installed application with everything present
in a folder nothing looks in: a desktop UI whose every icon is a Font Awesome
codepoint opening with no icons at all, and `render-site` warning about
webfonts it was standing next to. `makensis` exited 0.

Nothing in the family had hit it because every other `ships_beside_binary`
entry was a single top-level segment, with no interior separator to get wrong.
Void Hormiga's `vendor/fonts/` was the first nested one, and it will not be the
last — the field is general, and the next project to ship `assets/themes/` or
`share/locale/` would have got the same silent result.

So the fix is a chokepoint rather than a line: every path written into the
emitted script goes through one `nsis_path()`, including the `File /r` argument
that currently tolerates either form, because **a directive that happens to
forgive an input is not a contract**. The toy family now ships a nested folder
of its own, and `nsis_smoke` asserts that no quoted argument to any
path-resolving directive in the whole script contains a forward slash — the
class made unreachable, rather than the instance fixed.

It was found by a person installing the thing and opening it
([distribution](distribution.md)'s download-page routine, "install it and open
it"), which is the argument for that step existing: it is the only check in the
list that looks at what a stranger would actually get.

# 4. The hub is a file

> a part of me is thinking we might need a void hub or something, but if they
> could be stand alone downloads, that'd be much better.

**Standalone, and the hub is a document.**

A hub application is an application you must install before the application you
actually wanted — the same bootstrap Void Mago refuses for itself, for the same
reason. And the things a hub would give a *user* are, on inspection, either
unnecessary or not an application:

| what a hub would do | why not |
|---|---|
| a shared runtime cache | side-by-side installs each carry their own copy, and shared runtimes are what DLL hell is made of. `libvoidcore.dll` is 249 KB. |
| one place to browse what exists | that is a list, and a list is a file |
| update checking | every application already runs; the one that wants the update is the one that can ask |
| a single install for the suite | `mago wizard` already emits a multi-application installer when you want one. That is a *choice at build time*, not a resident program. |

So: **standalone installers, and one static `void-updates.json` beside them.**
`mago feed` writes it. No server, no account, no second thing to keep running —
the same conclusion [distribution](distribution.md) reached for hosting, from a
different direction.

## What the feed carries that a version number cannot

```json
{
  "feed": "void-updates/0.1",
  "applications": {
    "voidhormiga": {
      "latest": "0.1.1",
      "releases": [{
        "version": "0.1.1",
        "change": "compatible",
        "summary": "…",
        "behavior_changes": [
          { "what": "…", "who_is_affected": "…" }
        ],
        "artifacts": {
          "windows-x64": {
            "file": "VoidHormiga-0.1.1-windows-x64-setup.exe",
            "url": "…", "bytes": 0, "sha256": "…", "signature": null
          }
        }
      }]
    }
  }
}
```

`behavior_changes` is the point. A release block already declares the things
that moved *without a signature moving* — the category a resolver structurally
cannot see — so those travel into the feed, and the prompt a person sees can be

> **Void Hormiga 0.1.1 is available.**
> · `link --weight` now refuses a non-number — anything relying on the old
> silent `0.0` is affected.

instead of "a new version is available". **That is what "never silently" means
in practice**, and it only works because the sentence was written somewhere a
machine could carry it.

`signature` is present and `null` on purpose: absent would suggest signing was
never intended; null says it is intended and not yet done.

# 5. What Mago emits, and what an application implements

The split is sharp and worth stating, because most of the update work is *not*
Mago's.

| | Mago | the application |
|---|---|---|
| what files ship | `mago plan`, `mago stage` | — |
| the installer | `mago wizard` → NSIS | — |
| what exists | `mago feed` → `void-updates.json` | — |
| what is installed here | the receipt, written at install time | reads it to know its own version |
| fetching the feed | — | an HTTP GET at startup |
| comparing versions | — | its own version vs `latest` |
| **asking the person** | — | **the whole point, and never skippable** |
| downloading + verifying | — | the `sha256` in the feed |
| running the installer | — | side-by-side, so the running copy survives |

Mago is a **build-time** tool. It never runs on the user's machine and it is not
the updater — an update client that needed Mago installed would be one more
bootstrap. What Mago owns is the *format* and the *facts*, because those need
every manifest in the family and the application only needs its own.

# 6. What is still missing

1. **Signing.** Every manifest says `"signed": "ed25519"` and nothing
   implements it. A hash says the bytes arrived intact; it says nothing about
   who produced them, and a feed served from the same place as the file it
   describes can have both rewritten together. Until signing exists the feed is
   a convenience, not a security boundary — and SmartScreen will warn on every
   download regardless, which is [distribution](distribution.md) §3's argument
   for establishing the identity early because its value is accumulated
   reputation.
2. **`mago status`** — read an installed receipt on a machine and say what is
   there. The receipt is written and nothing reads it.
3. **Attaching the stable copy.** Mago names it and tells you when it is
   missing or stale; making it is still a human duplicating a file at release
   time. A `wizard` flag emitting both names, so `makensis` produces the
   second copy, would close the last hand-run step — worth doing once a
   release has actually been through the checked version.
4. **Applying an update to a running install.** Windows locks a running binary.
   Side-by-side sidesteps most of it (the new version is a new folder), but the
   shortcut has to be repointed and the old version eventually removed.
   Velopack implements exactly this, which remains the argument for emitting
   into it rather than writing it.
5. **A second platform.** Everything here is NSIS and PE imports. The macOS and
   Linux equivalents are different enough to be separate work, and neither has
   a consumer yet.

See also [[distribution]], [[blast-radius]], [[broadcast]].
