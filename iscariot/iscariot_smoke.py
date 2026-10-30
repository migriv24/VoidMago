"""iscariot_smoke.py -- a miniature repository with one of everything, and the
life of a flag: raised, claimed fixed and verified, claimed fixed and reopened,
overridden and never raised again.

Every value that would trip the scan is assembled at run time, so this file
does not flag itself. MAGO_HOME and the database are a temp directory: the
real ~/.mago is never read or written.
"""
import os
import random
import sqlite3
import string
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)


def check(cond, what):
    if not cond:
        print(f'FAIL: {what}')
        sys.exit(1)
    print(f'ok    {what}')


def run(home, *args):
    env = dict(os.environ, MAGO_HOME=home, ISCARIOT_DB=os.path.join(home, 'iscariot.db'),
               ISCARIOT_PRIVATE_TERMS=os.path.join(home, 'private-terms'))
    r = subprocess.run([sys.executable, os.path.join(HERE, 'iscariot.py'), *args], env=env,
                       capture_output=True, text=True, encoding='utf-8')
    return r.returncode, r.stdout


def flags(home):
    con = sqlite3.connect(os.path.join(home, 'iscariot.db'))
    con.row_factory = sqlite3.Row
    try:
        return {(r['rule'], r['path']): dict(r) for r in con.execute('select * from flags')}
    finally:
        con.close()  # Windows will not delete an open database


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)


def main():
    rng = random.Random(7)
    token = 'gh' + 'p_' + ''.join(rng.choice(string.ascii_letters + string.digits) for _ in range(36))
    home_dir = 'C:' + '\\Users\\' + 'ana\\Projects'

    with tempfile.TemporaryDirectory() as tmp:
        home = os.path.join(tmp, 'home')
        repo = os.path.join(tmp, 'VoidToy')
        os.makedirs(home)
        write(os.path.join(home, 'private-terms'), '# test list\nRiverton Clinic\n')
        write(os.path.join(repo, 'void.json'), '{"name": "voidtoy"}\n')
        write(os.path.join(repo, 'src', 'client.py'), f'TOKEN = "{token}"\n')
        write(os.path.join(repo, 'okf', 'log.md'), 'Learned from the Riverton Clinic database: dates drift.\n')
        write(os.path.join(repo, 'okf', 'paths.md'), f'Built in `{home_dir}`.\n')
        write(os.path.join(repo, 'okf', 'fine.md'), 'Write to ana@example.org or call 541-555-0100.\n')
        write(os.path.join(repo, 'MESSAGE_FOR_VOIDTOY_a-letter-2026-01-01.md'), 'hello\n')
        subprocess.run(['git', 'init', '-q', repo], check=True)
        subprocess.run(['git', '-C', repo, 'add', 'void.json', 'src', 'okf'], check=True)
        subprocess.run(['git', '-C', repo, '-c', 'user.name=t', '-c', 'user.email=t@example.org',
                        'commit', '-q', '-m', 'toy'], check=True)

        code, out = run(home, 'scan', repo, '--strict')
        check(code == 1, 'scan --strict fails while flags are open')
        check(token not in out, 'the matched credential is never printed')
        f = flags(home)
        check(('secret.github', 'src/client.py') in f, 'a token shape is tier 0')
        check(f[('secret.github', 'src/client.py')]['tier'] == 0, '...at tier 0')
        check(('file.local', 'MESSAGE_FOR_VOIDTOY_a-letter-2026-01-01.md') in f,
              'untracked correspondence that is not ignored is flagged')
        check(f[('file.local', 'MESSAGE_FOR_VOIDTOY_a-letter-2026-01-01.md')]['location'] == 'untracked, not ignored',
              '...and says it is untracked')
        check(('term.private', 'okf/log.md') in f, 'a private term in the OKF is flagged')
        check(('pii.home', 'okf/paths.md') in f, 'a home directory is flagged')
        check(not any(path == 'okf/fine.md' for _, path in f), 'example.org and 555 are not flagged')
        check(('ignore.missing', '.gitignore') in f, 'a .gitignore without the family lines is flagged')

        # A fix claimed and made: verified by the next scan.
        tid = f[('secret.github', 'src/client.py')]['id']
        run(home, 'fixed', str(tid), '--note', 'read from the environment', '--by', 'toy-agent')
        write(os.path.join(repo, 'src', 'client.py'), 'TOKEN = os.environ["TOY_TOKEN"]\n')
        run(home, 'scan', repo)
        check(flags(home)[('secret.github', 'src/client.py')]['status'] == 'fixed', 'a claimed fix is verified')

        # A fix claimed and not made: reopened.
        lid = f[('term.private', 'okf/log.md')]['id']
        run(home, 'fixed', str(lid), '--note', 'done, honest')
        run(home, 'scan', repo)
        check(flags(home)[('term.private', 'okf/log.md')]['status'] == 'open', 'a false claim is reopened')

        # An override is accepted and survives every later scan.
        pid = f[('pii.home', 'okf/paths.md')]['id']
        run(home, 'override', str(pid), '--note', 'a fictional user in a fixture')
        run(home, 'scan', repo)
        check(flags(home)[('pii.home', 'okf/paths.md')]['status'] == 'overridden', 'an override holds')

        # The one repair Iscariot makes: the .gitignore.
        run(home, 'ignore', repo, '--apply')
        run(home, 'scan', repo)
        f = flags(home)
        check(f[('file.local', 'MESSAGE_FOR_VOIDTOY_a-letter-2026-01-01.md')]['status'] == 'gone',
              'ignore --apply makes the correspondence local')

        # The message for the project's agent names places, never contents.
        code, out = run(home, 'notify', repo)
        check('okf/log.md:1' in out and 'Riverton' not in out, 'notify names the line and not the term')

    print('iscariot_smoke: all passed')


if __name__ == '__main__':
    main()
