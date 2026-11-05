#!/usr/bin/env python3
"""AguaraPOS has two version numbers, and this tool keeps both honest.

Product version (src/interfaces/version.h), MAJOR.MINOR.PATCH, what people see:
    patch   fixes, delivered as a patch
    minor   new features
    major   a change that takes planning at the store
  It changes only on purpose, with "bump": never because of a build.

API level (src/interfaces/apiversion.h), a single number, what modules are
checked against. Every interface ID ends in it, so a module built against
another level is skipped before any of its code loads. Anything in the public
headers is covered, including the data types passed between core and modules.
  The header records a fingerprint of the public headers, comments and spacing
  aside. If they change, "check" fails until "api" raises the level.

The two are independent: raising the level costs a rebuild and nothing else,
because the installer and every patch carry all the modules.

    python autoVersion.py               show the state, change nothing
    python autoVersion.py check         fail if the API changed without "api"
    python autoVersion.py check --release
                                        also fail if this version was already
                                        tagged on different code
    python autoVersion.py api           raise the API level
    python autoVersion.py bump patch    or minor, or major
    python autoVersion.py tag           tag the commit with vX.Y.Z (no push)

No dependencies but Python 3 (and git for tags). Works from any folder.
"""

import sys

if sys.version_info[0] < 3:
    sys.exit('autoVersion.py needs Python 3. The MinGW in Qt brings a Python 2 named python.')

import argparse
import hashlib
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parent
INTERFACES = ROOT / 'src' / 'interfaces'
VERSION_H = INTERFACES / 'version.h'
API_H = INTERFACES / 'apiversion.h'
# These two hold the numbers: they aren't part of what the numbers describe.
NOT_API = {VERSION_H.name, API_H.name}

COMMENT_OR_LITERAL = re.compile(
    r'''"(?:\\.|[^"\\\n])*"|'(?:\\.|[^'\\\n])*'|//[^\n]*|/\*.*?\*/''', re.S)


class Stop(Exception):
    """Something the person running the tool has to fix."""


def read(path):
    # newline='': keep each file's line endings when it's written back
    return path.read_text(encoding='utf-8', newline='')


def write(path, text):
    path.write_text(text, encoding='utf-8', newline='')


def without_comments(source):
    def keep_literals(match):
        return ' ' if match.group(0)[0] == '/' else match.group(0)
    return ' '.join(COMMENT_OR_LITERAL.sub(keep_literals, source).split())


def fingerprint():
    digest = hashlib.sha256()
    for header in sorted(INTERFACES.glob('*.h')):
        if header.name not in NOT_API:
            digest.update(header.name.encode())
            digest.update(without_comments(read(header)).encode())
    return digest.hexdigest()[:16]


def product_version():
    text = read(VERSION_H)
    parts = []
    for part in ('MAJOR', 'MINOR', 'PATCH'):
        found = re.search(r'^#define VERSION_%s (\d+)\s*$' % part, text, re.M)
        if not found:
            raise Stop('no VERSION_%s in %s' % (part, VERSION_H))
        parts.append(int(found.group(1)))
    return parts


def api_state():
    text = read(API_H)
    level = re.search(r'^#define AGUARA_API_VERSION "(\d+)"', text, re.M)
    recorded = re.search(r'^// fingerprint: ([0-9a-f]+)', text, re.M)
    if not level or not recorded:
        raise Stop('no API level or fingerprint in %s' % API_H)
    return int(level.group(1)), recorded.group(1)


def api_changed():
    return api_state()[1] != fingerprint()


def require_api_recorded():
    if api_changed():
        raise Stop('the public headers changed: run "python autoVersion.py api" '
                   'to raise the API level (level %d now)' % api_state()[0])


def git(*args):
    """The output of a git command, or None if git can't answer."""
    try:
        done = subprocess.run(['git', '-C', str(ROOT)] + list(args),
                              stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                              universal_newlines=True)
    except OSError:
        return None
    return done.stdout.strip() if done.returncode == 0 else None


def tag_name():
    return 'v%d.%d.%d' % tuple(product_version())


def tag_commit():
    return git('rev-parse', '--verify', '--quiet', tag_name() + '^{commit}')


def tree_is_clean():
    return git('status', '--porcelain') == ''


def require_unreleased():
    """Two different programs must never share a version number."""
    tagged = tag_commit()
    if tagged and (tagged != git('rev-parse', 'HEAD') or not tree_is_clean()):
        raise Stop('%s is already released and this code is different: '
                   'run "python autoVersion.py bump patch"' % tag_name())


def cmd_status(_):
    level, _recorded = api_state()
    print('Version  %s' % tag_name()[1:])
    print('API      level %d%s' % (level, ' (headers changed: run "api")' if api_changed() else ''))
    if tag_commit():
        same = tag_commit() == git('rev-parse', 'HEAD') and tree_is_clean()
        print('Tag      %s, %s' % (tag_name(), 'this code' if same else 'older code: bump before releasing'))
    else:
        print('Tag      none yet for this version')


def cmd_check(args):
    require_api_recorded()
    if args.release:
        require_unreleased()
    print('OK: version %s, API level %d' % (tag_name()[1:], api_state()[0]))


def cmd_api(_):
    level, _recorded = api_state()
    current = fingerprint()
    if current == _recorded:
        raise Stop('the public headers have not changed: the API stays at level %d' % level)
    text = read(API_H)
    text = re.sub(r'^(#define AGUARA_API_VERSION )"\d+"', r'\g<1>"%d"' % (level + 1), text, flags=re.M)
    text = re.sub(r'^(// fingerprint: )[0-9a-f]+', r'\g<1>' + current, text, flags=re.M)
    write(API_H, text)
    print('API level %d -> %d. Rebuild everything: old modules no longer load.' % (level, level + 1))


def cmd_bump(args):
    require_api_recorded()
    major, minor, patch = old = product_version()
    if args.part == 'major':
        new = [major + 1, 0, 0]
    elif args.part == 'minor':
        new = [major, minor + 1, 0]
    else:
        new = [major, minor, patch + 1]
    text = read(VERSION_H)
    for part, number in zip(('MAJOR', 'MINOR', 'PATCH'), new):
        text = re.sub(r'^(#define VERSION_%s )\d+' % part, r'\g<1>%d' % number, text, flags=re.M)
    write(VERSION_H, text)
    print('%d.%d.%d -> %d.%d.%d' % tuple(old + new))
    print('Commit, build and test on a till, then: python autoVersion.py tag')


def cmd_tag(_):
    require_api_recorded()
    name = tag_name()
    if git('rev-parse', '--git-dir') is None:
        raise Stop('not a git repository, or git is missing')
    if tag_commit():
        raise Stop('%s already exists' % name)
    if not tree_is_clean():
        raise Stop('commit your changes first: a tag must name exactly what was released')
    if git('tag', '-a', name, '-m', 'AguaraPOS ' + name[1:]) is None:
        raise Stop('git could not create %s' % name)
    print('Tagged %s. To publish it: git push origin %s' % (name, name))


def main():
    parser = argparse.ArgumentParser(description='AguaraPOS versions: product version and API level.')
    commands = parser.add_subparsers(dest='command')
    commands.add_parser('status').set_defaults(run=cmd_status)
    check = commands.add_parser('check')
    check.add_argument('--release', action='store_true')
    check.set_defaults(run=cmd_check)
    commands.add_parser('api').set_defaults(run=cmd_api)
    bump = commands.add_parser('bump')
    bump.add_argument('part', choices=['major', 'minor', 'patch'])
    bump.set_defaults(run=cmd_bump)
    commands.add_parser('tag').set_defaults(run=cmd_tag)
    args = parser.parse_args()

    try:
        getattr(args, 'run', cmd_status)(args)
    except Stop as problem:
        sys.exit('autoVersion: %s' % problem)


if __name__ == '__main__':
    main()
