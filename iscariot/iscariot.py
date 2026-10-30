"""iscariot -- the Void family's check that no repository betrays the people it served.

Every Void repository is public, and most of them ship their okf/ beside the
binary. An application that publishes a client's name, a credential or an
agent's local correspondence has betrayed whoever trusted it with that, which
is where the name comes from. Void Hormiga learned this twice and turned the
rule into tools/lint_public.py; Iscariot is that lint for the whole family,
plus a memory of what it found and what each project's agent did about it.

WHAT IS CHECKED, by tier (0 is the most urgent):

  0 credential   token and key shapes (GitHub, AWS, Slack, OpenAI, Anthropic,
                 Google, Stripe, private-key blocks), and a secret assigned to
                 a key-like name
  1 secret file  a file that holds secrets by its nature (.env, *.pem, a token
                 file) that is tracked, or that `git add -A` would track; and a
                 .gitignore missing the family's secret patterns
  2 local only   correspondence (MESSAGE_FOR_*, REPORT_FROM_*), okf/reports/,
                 databases, agent-local settings; and a .gitignore missing them
  3 private term a line naming anything on the private-terms lists; a commit
                 whose author or committer names one
  4 personal     email addresses outside example domains, phone numbers whose
                 exchange is not 555, home directories

Files scanned are what a push would publish: tracked files, plus untracked
files that are not ignored (the next `git add -A`). With --history, every line
ever added in any commit is scanned for tiers 0, 3 and 4, and every path ever
committed for tiers 1 and 2 -- because a first push publishes all of history.

THE PRIVATE TERMS are never in a repository, because the list is the secret:
MAGO_HOME/private-terms for the family, plus each project's own gitignored
.private-terms. Format as Void Hormiga's: one term per line, matched ignoring
case; `re:` starts a regular expression; `#` starts a comment.

FLAGS live in MAGO_HOME/iscariot.db (SQLite), outside every repository. A flag
stores where (path and line), which rule, and a hash of the line. It never
stores or prints the matched text: a report that echoed a phone number would
publish it a second time.

Iscariot repairs nothing. A flag is resolved by the agent responsible for that
project, in one of two ways:

  fixed ID --note "what was done"   the next scan verifies it is gone, and
                                    reopens it if it is not
  override ID --note "why"          the flag is wrong; it is not raised again

    python iscariot/iscariot.py scan                 every Void project
    python iscariot/iscariot.py scan voidsquid --history
    python iscariot/iscariot.py flags voidsquid
    python iscariot/iscariot.py notify voidsquid --write
"""
import argparse
import datetime
import hashlib
import json
import math
import os
import re
import sqlite3
import subprocess
import sys

TIER_NAMES = {0: 'credential', 1: 'secret file', 2: 'local only', 3: 'private term', 4: 'personal'}

# Third-party text carries its own authors' addresses and test keys.
SKIP_SEGMENTS = {'vendor', 'third_party', 'thirdparty', 'external', 'node_modules', '.git'}
SKIP_TREE_DIRS = SKIP_SEGMENTS | {'build', 'out', 'stage', 'dist', 'target', '__pycache__', '.vs', '.idea'}
MAX_BYTES = 4 * 1024 * 1024
BINARY_EXT = {'.pdf', '.png', '.jpg', '.jpeg', '.gif', '.webp', '.ico', '.bmp', '.ttf', '.otf', '.woff', '.woff2',
              '.zip', '.gz', '.7z', '.exe', '.dll', '.so', '.dylib', '.a', '.lib', '.obj', '.o', '.pyc', '.wav',
              '.mp3', '.ogg', '.flac', '.mp4', '.mov', '.webm', '.glb', '.fbx', '.blend', '.psd', '.apk'}


# ---------------------------------------------------------------- rules

SECRET_SHAPES = [
    ('secret.github', re.compile(r'\b(gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{40,})\b')),
    ('secret.aws', re.compile(r'\b(AKIA|ASIA)[0-9A-Z]{16}\b')),
    ('secret.slack', re.compile(r'\bxox[abprs]-[A-Za-z0-9-]{10,}')),
    ('secret.anthropic', re.compile(r'\bsk-ant-[A-Za-z0-9_-]{20,}')),
    ('secret.openai', re.compile(r'\bsk-(proj-)?[A-Za-z0-9_-]{32,}')),
    ('secret.google', re.compile(r'\bAIza[0-9A-Za-z_-]{35}\b')),
    ('secret.stripe', re.compile(r'\b[sr]k_live_[0-9A-Za-z]{20,}')),
    ('secret.private-key', re.compile(r'-----BEGIN ([A-Z]+ )?PRIVATE KEY-----')),
]
SECRET_ASSIGN = re.compile(
    r'(?i)\b[\w.-]*(api[_-]?key|secret|token|passw(or)?d|client[_-]?secret|auth[_-]?key)[\w.-]*'
    r'["\']?\s*[:=]\s*["\']([^"\'\s]{16,})["\']')
PLACEHOLDER = re.compile(r'(?i)(x{4,}|your|example|placeholder|changeme|dummy|fake|test|sample|redacted'
                         r'|0123456789|abcdef|\$\{|\{\{|<|%s|\*{3,})')

SECRET_FILES = re.compile(
    r'(^|/)(\.env(\.[^/]*)?|[^/]*\.(pem|p12|pfx|jks|keystore|ppk|peerkey)|id_(rsa|dsa|ecdsa|ed25519)(\.pub)?'
    r'|credentials[^/]*\.json|[^/]*client_secret[^/]*\.json|\.npmrc|\.pypirc|\.netrc'
    r'|git_secret\.txt|KEEP_HIDDEN\.txt|[^/]*\.secret|secrets?\.(json|ya?ml|toml|txt))$', re.I)
SECRET_FILE_OK = re.compile(r'(^|/)\.env\.(example|sample|template|dist)$|\.pub$', re.I)

LOCAL_FILES = [
    (re.compile(r'(^|/)(MESSAGE_FOR|REPORT_FROM)_[^/]*$'), 'correspondence between agents stays local'),
    (re.compile(r'(^|/)DAMAGE-REPORT[^/]*$', re.I), 'a damage report stays local'),
    (re.compile(r'(^|/)okf/reports/'), 'a report about somebody\'s database stays beside that database'),
    (re.compile(r'\.(db|sqlite3?|miga|bkp|merging)$', re.I), 'a database or its sidecar'),
    (re.compile(r'(^|/)\.private-terms$'), 'the private-terms list is itself the secret'),
    (re.compile(r'(^|/)\.claude/settings\.local\.json$'), 'an agent\'s local settings'),
]

# What every Void repository's .gitignore must cover: a probe name, its tier,
# and the line to add when it does not.
REQUIRED_IGNORES = [
    ('.env', 1, '.env'),
    ('git_secret.txt', 1, 'git_secret.txt'),
    ('KEEP_HIDDEN.txt', 1, 'KEEP_HIDDEN.txt'),
    ('server.pem', 1, '*.pem'),
    ('MESSAGE_FOR_X_y-2026-01-01.md', 2, 'MESSAGE_FOR_*.md'),
    ('REPORT_FROM_X_y-2026-01-01.md', 2, 'REPORT_FROM_*.md'),
    ('.private-terms', 2, '.private-terms'),
    ('.claude/settings.local.json', 2, '.claude/settings.local.json'),
]

EMAIL = re.compile(r'[A-Za-z0-9._%+-]+@([A-Za-z0-9-]+\.)+[A-Za-z]{2,}')
EMAIL_OK = re.compile(r'@(([A-Za-z0-9-]+\.)*(example\.(org|com|net)|example|invalid|test|localhost)'
                      r'|users\.noreply\.github\.com|noreply\.github\.com|anthropic\.com)$', re.I)
PHONE = re.compile(r'(?<![\d.\w-])(\(\d{3}\) ?|\d{3}[-.])(\d{3})[-.]\d{4}(?![\d\w])')
HOME = re.compile(r'(?:(?<![A-Za-z0-9])[A-Za-z]:[\\/]+Users[\\/]+'
                  r'|(?<![A-Za-z0-9_.-])/(?:c/Users|home|Users)/)([A-Za-z0-9._-]+)')
HOME_OK = {'', 'name', 'someone', 'somebody', 'you', 'user', 'username', 'me', 'public', 'default',
           'runner', 'shared', 'example', 'runneradmin', 'all', '...'}


def entropy(s):
    counts = {c: s.count(c) for c in set(s)}
    return -sum(n / len(s) * math.log2(n / len(s)) for n in counts.values())


def scan_line(line, terms, tiers=(0, 3, 4)):
    """[(rule, tier, reason)] for one line. Never returns the matched text."""
    hits = []
    if 0 in tiers:
        for rule, rx in SECRET_SHAPES:
            for m in rx.finditer(line):
                if not PLACEHOLDER.search(m.group(0)):
                    hits.append((rule, 0, 'something shaped like a credential'))
        for m in SECRET_ASSIGN.finditer(line):
            value = m.group(3)
            if not PLACEHOLDER.search(value) and entropy(value) > 3.5:
                hits.append(('secret.assignment', 0, 'a secret-looking value assigned to a key-like name'))
    if 3 in tiers and terms and any(t.search(line) for t in terms):
        hits.append(('term.private', 3, 'a private term (from the private-terms lists)'))
    if 4 in tiers:
        for m in EMAIL.finditer(line):
            if not EMAIL_OK.search(m.group(0)):
                hits.append(('pii.email', 4, 'an email address outside the example domains'))
        for m in PHONE.finditer(line):
            if m.group(2) != '555':
                hits.append(('pii.phone', 4, 'a phone number (fixtures use 555-01xx)'))
        for m in HOME.finditer(line):
            who = m.group(1).strip('.').lower()
            if who not in HOME_OK and not who.startswith('<'):
                hits.append(('pii.home', 4, 'a home directory: use a repo-relative path or a placeholder'))
    return hits


def path_rules(path):
    """[(rule, tier, reason)] for a file's path alone."""
    hits = []
    if SECRET_FILES.search(path) and not SECRET_FILE_OK.search(path):
        hits.append(('file.secret', 1, 'a file that holds secrets by its nature'))
    for rx, why in LOCAL_FILES:
        if rx.search(path):
            hits.append(('file.local', 2, why))
    return hits


# ---------------------------------------------------------------- where things live

def mago_home():
    explicit = os.environ.get('MAGO_HOME')
    if explicit:
        return explicit
    return os.path.join(os.path.expanduser('~'), '.mago')


def load_terms(project_dir=None):
    """Compiled patterns from the family list and the project's own list."""
    sources = [os.environ.get('ISCARIOT_PRIVATE_TERMS') or os.path.join(mago_home(), 'private-terms')]
    if project_dir:
        sources.append(os.path.join(project_dir, '.private-terms'))
    terms = []
    for path in sources:
        if not os.path.isfile(path):
            continue
        with open(path, encoding='utf-8') as f:
            for line in f:
                line = line.split('#', 1)[0].strip() if not line.lstrip().startswith('re:') else line.strip()
                if line.startswith('re:'):
                    terms.append(re.compile(line[3:], re.I))
                elif line:
                    terms.append(re.compile(re.escape(line), re.I))
    return terms


def roots():
    path = os.path.join(mago_home(), 'roots.json')
    try:
        with open(path, encoding='utf-8') as f:
            return [r['path'] for r in json.load(f).get('roots', [])]
    except (OSError, ValueError, KeyError):
        # beside this checkout, as `mago` does when nothing is registered
        return [os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))]


def project_id(folder):
    try:
        with open(os.path.join(folder, 'void.json'), encoding='utf-8') as f:
            name = json.load(f).get('name')
            if name:
                return name
    except (OSError, ValueError):
        pass
    return os.path.basename(folder).replace(' ', '').lower()


def discover(root_list=None):
    """{id: folder} for every Void project, by Mago's rule: a void.json, or a
    folder whose name, spaces removed, starts with "void". A folder named
    `*-private` is skipped: it exists to hold what must never be published."""
    found = {}
    for root in root_list or roots():
        if not os.path.isdir(root):
            continue
        for name in sorted(os.listdir(root)):
            folder = os.path.join(root, name)
            if not os.path.isdir(folder):
                continue
            if name.lower().endswith('-private'):
                continue  # kept apart from every repository on purpose; never published
            if os.path.isfile(os.path.join(folder, 'void.json')) or name.replace(' ', '').lower().startswith('void'):
                found.setdefault(project_id(folder), folder)
    return found


# ---------------------------------------------------------------- git

def git(folder, *args, check=False):
    r = subprocess.run(['git', '-c', 'core.quotepath=off', *args], cwd=folder, capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(r.stderr.decode('utf-8', 'replace').strip())
    return r


def is_repo(folder):
    return os.path.isdir(os.path.join(folder, '.git'))


def published_files(folder):
    """[(path, tracked)] for what a push would publish, or a tree walk when the
    folder is not a repository yet (everything would be added)."""
    if is_repo(folder):
        tracked = git(folder, 'ls-files', '-z').stdout.decode('utf-8', 'replace').split('\0')
        loose = git(folder, 'ls-files', '-z', '--others', '--exclude-standard').stdout.decode('utf-8', 'replace').split('\0')
        return [(p, True) for p in tracked if p] + [(p, False) for p in loose if p]
    out = []
    for dirpath, dirnames, filenames in os.walk(folder):
        dirnames[:] = [d for d in dirnames if d.lower() not in SKIP_TREE_DIRS and not d.lower().startswith('build')]
        for name in filenames:
            out.append((os.path.relpath(os.path.join(dirpath, name), folder).replace('\\', '/'), False))
    return out


def skipped(path):
    return any(seg.lower() in SKIP_SEGMENTS for seg in path.split('/')[:-1])


def read_text(folder, path):
    if os.path.splitext(path)[1].lower() in BINARY_EXT:
        return None
    full = os.path.join(folder, path)
    try:
        if os.path.getsize(full) > MAX_BYTES:
            return None
        with open(full, 'rb') as f:
            data = f.read()
    except OSError:
        return None
    if b'\0' in data[:8192]:
        return None
    return data.decode('utf-8', 'replace')


# ---------------------------------------------------------------- the scan

def fingerprint(project, rule, path, text, nth):
    h = hashlib.sha256(f'{project}\0{rule}\0{path}\0{text.strip()}\0{nth}'.encode('utf-8', 'replace'))
    return h.hexdigest()[:24]


def line_hash(text):
    return hashlib.sha256(text.strip().encode('utf-8', 'replace')).hexdigest()[:16]


def scan_project(pid, folder, terms, history=False):
    """[finding dict] for one project."""
    findings = []
    seen = {}

    def add(rule, tier, reason, path, line=0, text='', where='tree'):
        key = (rule, path, text.strip())
        seen[key] = seen.get(key, 0) + 1
        findings.append({'fingerprint': fingerprint(pid, rule, path, text, seen[key]), 'project': pid,
                         'rule': rule, 'tier': tier, 'reason': reason, 'path': path, 'line': line,
                         'line_hash': line_hash(text) if text else '', 'where': where})

    repo = is_repo(folder)
    for path, tracked in published_files(folder):
        where = 'tracked' if tracked else ('untracked, not ignored' if repo else 'no repository yet')
        for rule, tier, why in path_rules(path):
            add(rule, tier, why, path, where=where)
        if terms and any(t.search(path) for t in terms):
            add('term.path', 3, 'a file or folder name carries a private term', path, where=where)
        if skipped(path):
            continue
        text = read_text(folder, path)
        if text is None:
            continue
        for n, line in enumerate(text.splitlines(), 1):
            for rule, tier, why in scan_line(line, terms):
                add(rule, tier, why, path, n, line, where)

    if repo:
        for probe, tier, pattern in REQUIRED_IGNORES:
            if git(folder, 'check-ignore', '-q', '--no-index', probe).returncode != 0:
                add('ignore.missing', tier, f'.gitignore does not cover `{pattern}`', '.gitignore',
                    text=pattern, where='ignore')
        authors = git(folder, 'log', '--all', '--format=%an <%ae>%n%cn <%ce>').stdout.decode('utf-8', 'replace')
        for who in sorted(set(authors.splitlines())):
            if who and terms and any(t.search(who) for t in terms):
                add('git.author', 3, 'a commit author or committer identity names a private term; '
                    'every commit publishes it, and only a history rewrite removes it',
                    '(git history)', text=who, where='authors')
        if history:
            findings += scan_history(pid, folder, terms, add)
    return findings


def scan_history(pid, folder, terms, add):
    """Every line ever added, and every path ever committed. Reported once per
    (commit, path, rule), as `(history) <commit>:<path>`."""
    paths = git(folder, 'log', '--all', '--format=', '--name-only').stdout.decode('utf-8', 'replace')
    current = {p for p, _ in published_files(folder)}
    for path in sorted(set(p for p in paths.splitlines() if p)):
        if path in current:
            continue  # the tree scan already judged it
        for rule, tier, why in path_rules(path):
            add(rule, tier, why + ' (in history only)', '(history) ' + path, where='history')
    log = subprocess.Popen(['git', '-c', 'core.quotepath=off', 'log', '--all', '-p', '--no-ext-diff',
                            '--no-color', '--format=commit %h'], cwd=folder, stdout=subprocess.PIPE)
    commit, path, reported = '', '', set()
    for raw in log.stdout:
        line = raw.decode('utf-8', 'replace').rstrip('\n')
        if line.startswith('commit '):
            commit = line[7:].strip()
        elif line.startswith('+++ '):
            path = line[6:] if line.startswith('+++ b/') else line[4:]
        elif line.startswith('+') and not line.startswith('+++') and not skipped(path):
            for rule, tier, why in scan_line(line[1:], terms):
                key = (commit, path, rule)
                if key not in reported:
                    reported.add(key)
                    add(rule, tier, why + ' (in history)', f'(history) {commit}:{path}', text=line[1:],
                        where='history')
    log.wait()
    return []


# ---------------------------------------------------------------- the flag database

SCHEMA = '''
create table if not exists flags (
  id integer primary key,
  fingerprint text unique not null,
  project text not null,
  rule text not null,
  tier integer not null,
  reason text not null,
  path text not null,
  line integer not null,
  line_hash text,
  location text,
  status text not null,          -- open | claimed | fixed | gone | overridden
  first_seen text not null,
  last_seen text not null,
  note text,
  resolved_by text,
  resolved_at text
);
create table if not exists events (
  id integer primary key,
  flag integer,
  at text not null,
  what text not null,
  note text,
  by text
);
create table if not exists scans (
  id integer primary key,
  project text not null,
  at text not null,
  head text,
  history integer not null,
  open integer not null,
  new integer not null
);
'''


def now():
    return datetime.datetime.now().isoformat(timespec='seconds')


def db():
    home = mago_home()
    os.makedirs(home, exist_ok=True)
    con = sqlite3.connect(os.environ.get('ISCARIOT_DB') or os.path.join(home, 'iscariot.db'))
    con.row_factory = sqlite3.Row
    con.executescript(SCHEMA)
    return con


def event(con, flag, what, note=None, by=None):
    con.execute('insert into events (flag, at, what, note, by) values (?,?,?,?,?)', (flag, now(), what, note, by))


def record(con, pid, folder, findings, history):
    """Fold one scan into the database. Returns (open, new) counts."""
    t = now()
    seen = set()
    new = 0
    for f in findings:
        seen.add(f['fingerprint'])
        row = con.execute('select id, status from flags where fingerprint=?', (f['fingerprint'],)).fetchone()
        if row is None:
            cur = con.execute(
                'insert into flags (fingerprint, project, rule, tier, reason, path, line, line_hash, location,'
                ' status, first_seen, last_seen) values (?,?,?,?,?,?,?,?,?,?,?,?)',
                (f['fingerprint'], pid, f['rule'], f['tier'], f['reason'], f['path'], f['line'],
                 f['line_hash'], f['where'], 'open', t, t))
            event(con, cur.lastrowid, 'raised')
            new += 1
            continue
        con.execute('update flags set line=?, location=?, last_seen=? where id=?',
                    (f['line'], f['where'], t, row['id']))
        if row['status'] in ('claimed', 'fixed', 'gone'):
            con.execute('update flags set status=? where id=?', ('open', row['id']))
            event(con, row['id'], 'reopened',
                  'a fix was claimed and the finding is still there' if row['status'] == 'claimed'
                  else 'it came back')
    # Everything this project had open or claimed that the scan no longer sees.
    # A history scan sees everything a tree scan does, but not the reverse.
    for row in con.execute('select id, status, location from flags where project=? and status in (?,?)',
                           (pid, 'open', 'claimed')).fetchall():
        fp = con.execute('select fingerprint from flags where id=?', (row['id'],)).fetchone()[0]
        if fp in seen or (row['location'] == 'history' and not history):
            continue
        status = 'fixed' if row['status'] == 'claimed' else 'gone'
        con.execute('update flags set status=?, resolved_at=coalesce(resolved_at, ?) where id=?',
                    (status, t, row['id']))
        event(con, row['id'], 'verified fixed' if status == 'fixed' else 'gone without a claim')
    head = git(folder, 'rev-parse', '--short', 'HEAD').stdout.decode().strip() if is_repo(folder) else ''
    open_count = con.execute('select count(*) from flags where project=? and status in (?,?)',
                             (pid, 'open', 'claimed')).fetchone()[0]
    con.execute('insert into scans (project, at, head, history, open, new) values (?,?,?,?,?,?)',
                (pid, t, head, int(history), open_count, new))
    con.commit()
    return open_count, new


# ---------------------------------------------------------------- commands

def resolve_projects(names, all_projects):
    if not names:
        return all_projects
    out = {}
    for n in names:
        if os.path.isdir(n):
            folder = os.path.abspath(n)
            out[project_id(folder)] = folder
            continue
        key = n.lower().replace(' ', '')
        match = [k for k, v in all_projects.items()
                 if k == key or os.path.basename(v).replace(' ', '').lower() == key]
        if not match:
            sys.exit(f'iscariot: no Void project named {n!r} (try `iscariot projects`)')
        out[match[0]] = all_projects[match[0]]
    return out


def fmt_flag(r):
    where = f"{r['path']}:{r['line']}" if r['line'] else r['path']
    loc = '' if r['location'] in ('tracked', 'ignore', 'authors', 'history') else f"  [{r['location']}]"
    return f"  #{r['id']:<5} t{r['tier']} {r['rule']:<18} {where}  -- {r['reason']}{loc}"


def cmd_projects(args):
    for pid, folder in discover().items():
        state = 'git' if is_repo(folder) else 'no repository'
        remote = git(folder, 'remote', 'get-url', 'origin').stdout.decode().strip() if is_repo(folder) else ''
        print(f'  {pid:<24} {state:<14} {remote or "-"}')
    return 0


def cmd_terms(args):
    family = os.environ.get('ISCARIOT_PRIVATE_TERMS') or os.path.join(mago_home(), 'private-terms')
    print(f'  family list   {family}  ({len(load_terms()) if os.path.isfile(family) else "MISSING"} terms)')
    for pid, folder in discover().items():
        own = os.path.join(folder, '.private-terms')
        if os.path.isfile(own):
            print(f'  {pid:<13} {own}  ({len(load_terms(folder)) - len(load_terms())} terms)')
    return 0


def cmd_scan(args):
    projects = resolve_projects(args.projects, discover())
    con = db()
    worst = 0
    terms_path = os.environ.get('ISCARIOT_PRIVATE_TERMS') or os.path.join(mago_home(), 'private-terms')
    for pid, folder in projects.items():
        terms = load_terms(folder)
        findings = scan_project(pid, folder, terms, history=args.history)
        open_count, new = record(con, pid, folder, findings, args.history)
        rows = con.execute('select * from flags where project=? and status in (?,?) order by tier, path, line',
                           (pid, 'open', 'claimed')).fetchall()
        by_tier = {}
        for r in rows:
            by_tier[r['tier']] = by_tier.get(r['tier'], 0) + 1
        summary = '  '.join(f't{t}:{n}' for t, n in sorted(by_tier.items())) or 'clean'
        print(f'{pid:<24} {summary}' + (f'  ({new} new)' if new else ''))
        if args.verbose:
            for r in rows:
                print(fmt_flag(r))
        if rows:
            worst = max(worst, 1)
    if not os.path.isfile(terms_path):
        print(f'note: no family private-terms list at {terms_path}: names were not checked')
    return 1 if (args.strict and worst) else 0


def cmd_flags(args):
    con = db()
    statuses = ('open', 'claimed') if not args.all else ('open', 'claimed', 'fixed', 'gone', 'overridden')
    q = f'select * from flags where status in ({",".join("?" * len(statuses))})'
    params = list(statuses)
    if args.project:
        pid = next(iter(resolve_projects([args.project], discover())))
        q += ' and project=?'
        params.append(pid)
    if args.tier is not None:
        q += ' and tier<=?'
        params.append(args.tier)
    rows = con.execute(q + ' order by project, tier, path, line', params).fetchall()
    if args.json:
        print(json.dumps([dict(r) for r in rows], indent=2))
        return 0
    project = None
    for r in rows:
        if r['project'] != project:
            project = r['project']
            print(project)
        print(fmt_flag(r) + (f"  [{r['status']}]" if r['status'] != 'open' else ''))
    if not rows:
        print('no flags')
    return 0


def cmd_show(args):
    con = db()
    r = con.execute('select * from flags where id=?', (args.id,)).fetchone()
    if r is None:
        sys.exit(f'iscariot: no flag #{args.id}')
    for k in ('id', 'project', 'status', 'tier', 'rule', 'reason', 'path', 'line', 'location',
              'first_seen', 'last_seen', 'note', 'resolved_by', 'resolved_at'):
        print(f'  {k:<12} {r[k] if r[k] is not None else "-"}')
    for e in con.execute('select * from events where flag=? order by id', (args.id,)):
        print(f"  {e['at']}  {e['what']}" + (f" by {e['by']}" if e['by'] else '') + (f": {e['note']}" if e['note'] else ''))
    return 0


def resolve_flags(args, status, what):
    con = db()
    for fid in args.ids:
        r = con.execute('select status from flags where id=?', (fid,)).fetchone()
        if r is None:
            print(f'iscariot: no flag #{fid}', file=sys.stderr)
            continue
        con.execute('update flags set status=?, note=?, resolved_by=?, resolved_at=? where id=?',
                    (status, args.note, args.by, now(), fid))
        event(con, fid, what, args.note, args.by)
        print(f'#{fid}: {status}')
    con.commit()
    return 0


def cmd_fixed(args):
    return resolve_flags(args, 'claimed', 'fix claimed')


def cmd_override(args):
    return resolve_flags(args, 'overridden', 'overridden')


def cmd_reopen(args):
    con = db()
    for fid in args.ids:
        con.execute('update flags set status=?, resolved_at=null where id=?', ('open', fid))
        event(con, fid, 'reopened', args.note, args.by)
    con.commit()
    return 0


def cmd_ignore(args):
    """The family's required .gitignore lines a project is missing; --apply
    appends them. A .gitignore is hygiene, not logic, so this is the one
    repair Iscariot makes itself."""
    for pid, folder in resolve_projects(args.projects, discover()).items():
        if not is_repo(folder):
            print(f'{pid:<24} no repository')
            continue
        missing = [(pattern, tier) for probe, tier, pattern in REQUIRED_IGNORES
                   if git(folder, 'check-ignore', '-q', '--no-index', probe).returncode != 0]
        print(f'{pid:<24} ' + (', '.join(p for p, _ in missing) if missing else 'complete'))
        if missing and args.apply:
            path = os.path.join(folder, '.gitignore')
            existing = ''
            if os.path.isfile(path):
                with open(path, encoding='utf-8', newline='') as f:
                    existing = f.read()
            nl = '\r\n' if '\r\n' in existing else '\n'
            block = (('' if not existing or existing.endswith(('\n', '\r\n')) else nl) + nl +
                     '# Never published (Void Mago\'s Iscariot): secrets, correspondence between' + nl +
                     '# agents, and the private-terms list.' + nl +
                     nl.join(p for p, _ in missing) + nl)
            with open(path, 'a', encoding='utf-8', newline='') as f:
                f.write(block)
    return 0


def cmd_notify(args):
    """A MESSAGE_FOR_<PROJECT> in the project's root listing its open flags,
    for the agent responsible for it. Correspondence is gitignored family-wide;
    Iscariot refuses to write one into a repository that would publish it."""
    pid, folder = next(iter(resolve_projects([args.project], discover()).items()))
    con = db()
    rows = con.execute('select * from flags where project=? and status in (?,?) order by tier, path, line',
                       (pid, 'open', 'claimed')).fetchall()
    # What is already in history, or in commit identities, only a rewrite and
    # a force-push remove. That is the author's decision, not the agent's.
    held = [r for r in rows if r['location'] in ('history', 'authors')]
    if not args.all:
        rows = [r for r in rows if r['location'] not in ('history', 'authors')]
    if not rows:
        print(f'{pid}: no open flags an agent can resolve, nothing to send'
              + (f' ({len(held)} in history, for the author)' if held else ''))
        return 0
    date = args.date or datetime.date.today().isoformat()
    try:
        me = os.path.relpath(os.path.abspath(__file__), folder).replace('\\', '/')
    except ValueError:  # another drive, on Windows: no relative path exists
        me = os.path.abspath(__file__).replace('\\', '/')
    lines = [f'# From Iscariot (Void Mago) to {pid}: {len(rows)} open flag(s)', '',
             f'**Date:** {date} · **From:** Void Mago\'s Iscariot · **Ask:** resolve each flag below, '
             'then answer it with one of the two commands at the end.', '',
             'Iscariot checks that nothing in a public Void repository betrays the people it served: '
             'credentials, agents\' correspondence, private names, personal data. It never quotes what it '
             'found, because quoting it would publish it a second time. Open the path at the line.', '',
             'Iscariot repaired nothing in your code. A hard-coded credential, or a private name inside '
             'logic, is yours to fix, because you know what depends on it. A credential that was ever '
             'committed must also be revoked: removing it from the tree leaves it in history.', '']
    if held and not args.all:
        lines += [f'{len(held)} more flag(s) are in this repository\'s history or commit identities. Only a '
                  'history rewrite removes those, which is the author\'s decision, so they are not listed '
                  'here and are not yours to answer.', '']
    for tier in sorted({r['tier'] for r in rows}):
        lines += [f'## Tier {tier}: {TIER_NAMES[tier]}', '', '| flag | where | rule | why |', '|---|---|---|---|']
        for r in rows:
            if r['tier'] == tier:
                where = f"`{r['path']}:{r['line']}`" if r['line'] else f"`{r['path']}`"
                lines.append(f"| #{r['id']} | {where} | {r['rule']} | {r['reason']} |")
        lines.append('')
    lines += ['## Answering a flag', '', 'From this repository\'s root:', '', '```',
              f'python {me} fixed <id> --by <you> --note "what you changed"',
              f'python {me} override <id> --by <you> --note "why the flag is wrong"',
              f'python {me} scan {pid}        # verifies every claimed fix', '```', '',
              'A claimed fix is verified by the next scan, and reopened if the finding is still there. '
              'An override is accepted as the truth and not raised again. Delete this message once it is '
              'answered.', '', '— Iscariot, for Void Mago', '']
    text = '\n'.join(lines)
    name = f"MESSAGE_FOR_{os.path.basename(folder).replace(' ', '').upper()}_iscariot-open-flags-{date}.md"
    if not args.write:
        print(text)
        return 0
    if is_repo(folder) and git(folder, 'check-ignore', '-q', '--no-index', name).returncode != 0:
        sys.exit(f'iscariot: {pid} does not gitignore MESSAGE_FOR_*.md; run `iscariot ignore {pid} --apply` first')
    with open(os.path.join(folder, name), 'w', encoding='utf-8') as f:
        f.write(text)
    print(os.path.join(folder, name))
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(prog='iscariot', description='The Void family\'s publication check.')
    sub = p.add_subparsers(dest='command', required=True)

    s = sub.add_parser('scan', help='scan projects and record what was found')
    s.add_argument('projects', nargs='*', help='project ids or folders (default: every Void project)')
    s.add_argument('--history', action='store_true', help='also scan every commit, as a first push would publish')
    s.add_argument('--strict', action='store_true', help='exit 1 when any flag is open')
    s.add_argument('-v', '--verbose', action='store_true', help='list each open flag')
    s.set_defaults(func=cmd_scan)

    s = sub.add_parser('flags', help='list flags (open ones, unless --all)')
    s.add_argument('project', nargs='?')
    s.add_argument('--all', action='store_true')
    s.add_argument('--tier', type=int, help='only this tier and more urgent')
    s.add_argument('--json', action='store_true')
    s.set_defaults(func=cmd_flags)

    s = sub.add_parser('show', help='one flag and its history')
    s.add_argument('id', type=int)
    s.set_defaults(func=cmd_show)

    for name, func, help_text in (('fixed', cmd_fixed, 'claim a fix; the next scan verifies it'),
                                  ('override', cmd_override, 'the flag is wrong; never raise it again'),
                                  ('reopen', cmd_reopen, 'undo a fix claim or an override')):
        s = sub.add_parser(name, help=help_text)
        s.add_argument('ids', nargs='+', type=int)
        s.add_argument('--note', required=name != 'reopen', help='what was done, or why')
        s.add_argument('--by', default=os.environ.get('ISCARIOT_AGENT'), help='who is answering')
        s.set_defaults(func=func)

    s = sub.add_parser('ignore', help='the required .gitignore lines a project lacks')
    s.add_argument('projects', nargs='*')
    s.add_argument('--apply', action='store_true', help='append them')
    s.set_defaults(func=cmd_ignore)

    s = sub.add_parser('notify', help='write the open flags into a project as a message')
    s.add_argument('project')
    s.add_argument('--write', action='store_true', help='write it into the project root (default: print)')
    s.add_argument('--all', action='store_true', help='include flags in history, which only the author can resolve')
    s.add_argument('--date')
    s.set_defaults(func=cmd_notify)

    sub.add_parser('projects', help='the Void projects Iscariot can see').set_defaults(func=cmd_projects)
    sub.add_parser('terms', help='where the private-terms lists are (never their contents)').set_defaults(func=cmd_terms)

    for stream in (sys.stdout, sys.stderr):
        stream.reconfigure(encoding='utf-8', errors='replace')
    args = p.parse_args(argv)
    return args.func(args)


if __name__ == '__main__':
    sys.exit(main())
