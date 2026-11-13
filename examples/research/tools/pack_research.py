# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
from pathlib import Path
import gzip
import json
import mimetypes

ROOT = Path(__file__).resolve().parents[1]
target = ROOT / 'main/assets.generated.hpp'
parts = ['// Generated; source: examples/research/web/. TechnicalShell asset provenance: docs/readme_runtime_ru.md.',
         '#pragma once', '#include <cstddef>', '#include <cstdint>', 'namespace research_assets {',
         'struct Asset {const char* path;const char* mime;const uint8_t* data;size_t size;};']
files = []
for path in sorted((ROOT / 'web').rglob('*')):
    if not path.is_file():
        continue
    relative = path.relative_to(ROOT / 'web').as_posix()
    mime = mimetypes.guess_type(relative)[0] or 'application/octet-stream'
    if relative.endswith('.js'):
        mime = 'text/javascript'
    data = gzip.compress(path.read_bytes(), compresslevel=9, mtime=0)
    key = f'file_{len(files)}'
    parts.append(f'inline constexpr uint8_t {key}[]={{')
    parts.extend(','.join(str(b) for b in data[n:n+40])+',' for n in range(0,len(data),40))
    parts.append('};')
    files.append((relative,mime,key))
parts.append('inline constexpr Asset files[]={')
for path,mime,key in files:
    route = '/' if path=='index.html' else '/'+path
    parts.append(f'{{{json.dumps(route)},{json.dumps(mime)},{key},sizeof({key})}},')
parts.extend(['};','}'])
target.parent.mkdir(parents=True, exist_ok=True)
content = '\n'.join(parts)+'\n'
if not target.exists() or target.read_text(encoding='utf8')!=content:
    target.write_text(content,encoding='utf8',newline='\n')
print(f'Research assets packed: {len(files)} files')
