#!/usr/bin/env python3
"""Takes the symbols out of a release build: they go to a file for the telemetry
server, and the binaries that remain have none. installer/build.sh runs it on the
staging folder, so nothing that reaches a till carries them.

    export-symbols.py --version 1.0.3-beta --output simbolos.json.gz FILE...

Needs nm and strip from MinGW, on PATH.

A crash report says "aguara.exe+0x1a2b", and each module's PE time stamp says
which build that was: a version number doesn't, because the same version gets
built many times. strip clears that stamp, so it's written back here. That way
the binaries of the full installer and of the patch have the same identity.
"""
import argparse
import gzip
import json
import os
import re
import struct
import subprocess
import sys
import time

# Labels in the middle of a function, not functions: the linker's marks for what
# comes from a DLL, and the compiler's for sections.
NOT_A_FUNCTION = re.compile(r'^(__fu\d+_|\.|__imp_|__head_|__nm_|___?(lib|dll)[\w]*_a_iname)')


def header(path):
    """The time stamp and the image base of a PE file, read from the file itself."""
    with open(path, 'rb') as source:
        data = source.read(4096)
    if data[:2] != b'MZ':
        raise SystemExit('%s is not a PE file' % path)
    pe = struct.unpack_from('<I', data, 0x3C)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise SystemExit('%s is not a PE file' % path)
    stamp = struct.unpack_from('<I', data, pe + 8)[0]
    optional = pe + 24
    magic = struct.unpack_from('<H', data, optional)[0]
    if magic == 0x10B:
        base = struct.unpack_from('<I', data, optional + 28)[0]
    elif magic == 0x20B:
        base = struct.unpack_from('<Q', data, optional + 24)[0]
    else:
        raise SystemExit('%s: unknown PE format' % path)
    return stamp, base, pe + 8


def run(*command):
    """A file just written can be locked for a moment, by the antivirus for one."""
    for attempt in range(6):
        done = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True, errors='replace')
        if done.returncode == 0:
            return done.stdout
        if attempt < 5:
            time.sleep(1)
    raise SystemExit('%s failed: %s' % (' '.join(command), done.stderr.strip()[:300]))


def symbols_of(path, base):
    rows = {}
    for line in run('nm', '-n', '--defined-only', '-C', path).splitlines():
        parts = line.split(' ', 2)
        if len(parts) != 3 or parts[1] not in ('t', 'T'):
            continue
        name = parts[2]
        if NOT_A_FUNCTION.match(name):
            continue
        if name.startswith('_') and '::' not in name and '(' not in name:
            name = name[1:]         # the prefix of 32-bit C names
        offset = int(parts[0], 16) - base
        if offset >= 0:
            rows.setdefault(offset, name[:1000])
    return sorted([offset, name] for offset, name in rows.items())


def strip(path):
    """Keeps the stamp, which is what says which build this is."""
    stamp, base, place = header(path)
    run('strip', path)
    for attempt in range(6):
        try:
            with open(path, 'r+b') as target:
                target.seek(place)
                target.write(struct.pack('<I', stamp))
            break
        except PermissionError:
            if attempt == 5:
                raise
            time.sleep(1)
    after = header(path)
    if after[0] != stamp or after[1] != base:
        raise SystemExit('%s changed its identity when stripped' % path)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('files', nargs='+')
    args = parser.parse_args()

    modules = []
    for path in args.files:
        stamp, base, _ = header(path)
        if stamp == 0:
            raise SystemExit('%s has no time stamp: it was stripped before this. Rebuild it.' % path)
        rows = symbols_of(path, base)
        if not rows:
            raise SystemExit('%s has no symbols: rebuild from clean, the release link must not strip.' % path)
        strip(path)
        name = os.path.basename(path)
        print('  %-34s %08x %7d symbols' % (name, stamp, len(rows)))
        modules.append({'name': name, 'stamp': '%08x' % stamp, 'symbols': rows})

    package = {'schema': 1, 'version': args.version, 'modules': modules}
    with gzip.open(args.output, 'wt', encoding='utf-8', compresslevel=9) as target:
        json.dump(package, target, separators=(',', ':'))
    print('%s: %.1f MB' % (args.output, os.path.getsize(args.output) / 1e6))


if __name__ == '__main__':
    sys.exit(main())
