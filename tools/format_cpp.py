# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
"""Check/apply the shared NaBrIks C/C++ profile; never process vendor/generated data."""
import argparse
from pathlib import Path
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
mode = parser.add_mutually_exclusive_group(required=True)
mode.add_argument('--check', action='store_true')
mode.add_argument('--apply', action='store_true')
mode.add_argument('--list', action='store_true', help='List the owned C/C++ scope without invoking the formatter')
parser.add_argument('--uncrustify', default=os.environ.get('UNCRUSTIFY') or shutil.which('uncrustify'))
args = parser.parse_args()
if not args.list and not args.uncrustify:
    parser.error('Install Uncrustify 0.83.0_f; use PATH, UNCRUSTIFY or --uncrustify')
version = ''
if not args.list:
    try:
        version = subprocess.check_output([args.uncrustify, '--version'], text=True).strip()
    except (OSError, subprocess.CalledProcessError) as error:
        parser.error(str(error))
    if not version.endswith('0.83.0_f'):
        parser.error('Expected Uncrustify 0.83.0_f, got ' + version)
    if not (ROOT / 'uncrustify.cfg').is_file():
        parser.error('Missing uncrustify.cfg')
paths = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z']).decode('utf8').split('\0')
excluded = {'vendor', 'third_party', 'bluetooth-sig', 'managed_components', 'node_modules', '.pio', 'local', 'build'}
extensions = {'.c', '.cc', '.cpp', '.cxx', '.h', '.hh', '.hpp', '.hxx'}
changed = failures = count = 0
for name in paths:
    path = ROOT / name
    if not name or not path.is_file() or path.suffix.lower() not in extensions:
        continue
    if excluded.intersection(part.lower() for part in Path(name).parts) or '.generated.' in path.name.lower() or path.name in ('assets.hpp', 'assets.generated.hpp'):
        continue
    count += 1
    if args.list:
        print(name)
        continue
    before = path.read_bytes()
    formatted = before
    # The canonical profile has a known second-pass nested-destructor case.
    # Reach a fixed point before writing, then --check verifies one further pass.
    for attempt in range(4 if args.apply else 1):
        result = subprocess.run([args.uncrustify, '-c', str(ROOT / 'uncrustify.cfg'),
                                 '-l', 'C' if path.suffix == '.c' else 'CPP'],
                                input=formatted, capture_output=True)
        if result.returncode:
            print(name + ': ' + result.stderr.decode('utf8', errors='replace'), file=sys.stderr)
            failures += 1
            break
        if result.stdout == formatted:
            break
        formatted = result.stdout
    else:
        if args.apply:
            print(name + ': formatter did not reach a fixed point', file=sys.stderr)
            failures += 1
            continue
    if result.returncode:
        continue
    if formatted != before:
        changed += 1
        if args.apply:
            path.write_bytes(formatted)
        else:
            print('Formatting differs: ' + name)
print(f'{version}: {count} own C/C++ files; {changed} changed/different; {failures} errors', file=sys.stderr if args.list else sys.stdout)
raise SystemExit(1 if failures or (args.check and changed) else 0)
