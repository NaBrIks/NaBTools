# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
"""Check portable Markdown file links, including case, without network access."""
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
paths = subprocess.check_output(
    ['git', '-C', str(ROOT), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']
).decode('utf8').split('\0')
errors = []
checked = 0
for relative in sorted(set(paths)):
    path = ROOT / relative
    if not relative or path.suffix.lower() != '.md' or not path.is_file() or relative.startswith('bluetooth-SIG/'):
        continue
    for match in re.finditer(r'!?\[[^\]\n]*\]\(([^)\n]+)\)', path.read_text(encoding='utf8')):
        target = match[1].strip().strip('<>')
        if urlsplit(target).scheme or target.startswith('#'):
            continue
        target = unquote(target.split('#', 1)[0])
        if not target:
            continue
        checked += 1
        resolved = (path.parent / target).resolve()
        if not resolved.is_relative_to(ROOT):
            errors.append(f'{relative}: nonportable path: {target}')
            continue
        if not resolved.exists():
            errors.append(f'{relative}: missing: {target}')
            continue
        # Resolve one component at a time: Windows exists() accepts wrong case.
        probe = ROOT
        for part in resolved.relative_to(ROOT).parts:
            if part not in {child.name for child in probe.iterdir()}:
                errors.append(f'{relative}: case mismatch: {target}')
                break
            probe /= part
if errors:
    raise SystemExit('\n'.join(errors))
print(f'PASS {checked} portable Markdown file links (including case)')
