# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
"""Validate name tables from the pinned Bluetooth SIG submodule."""
from pathlib import Path
import yaml

TABLES = {
    'company': ('assigned_numbers/company_identifiers/company_identifiers.yaml', 'company_identifiers', 'value'),
    'ad_type': ('assigned_numbers/core/ad_types.yaml', 'ad_types', 'value'),
    **{name: ('assigned_numbers/uuids/' + file + '.yaml', 'uuids', 'uuid') for name, file in {
        'service':'service_uuids', 'characteristic':'characteristic_uuids',
        'descriptor':'descriptors', 'declaration':'declarations', 'unit':'units',
        'service_class':'service_class', 'protocol':'protocol_identifiers',
        'member':'member_uuids', 'sdo':'sdo_uuids', 'browse_group':'browse_group_identifiers',
        'mesh_profile':'mesh_profile_uuids', 'object_type':'object_types',
    }.items()},
    'appearance': ('assigned_numbers/core/appearance_values.yaml', 'appearance_values', 'category'),
}

def read_table(base: Path, name: str):
    path, key, value = TABLES[name]
    rows = yaml.safe_load((base / path).read_text(encoding='utf8'))[key]
    out = {}
    for row in rows:
        code = int(row[value])
        if name == 'appearance':
            if not 0 <= code <= 1023:
                raise ValueError('Appearance category exceeds 10 bits')
            code <<= 6
            if code in out:
                raise ValueError('Duplicate appearance category')
            out[code] = row['name']
            for sub in row.get('subcategory', []):
                subvalue = int(sub['value'])
                if not 1 <= subvalue <= 63:
                    raise ValueError('Appearance subcategory must be in 1..63')
                subcode = code | subvalue
                if subcode in out: raise ValueError('Duplicate appearance code')
                out[subcode] = row['name'] + ': ' + sub['name']
        elif code in out:
            if name != 'ad_type' or code != 0x10:
                raise ValueError(f'Duplicate SIG code: {path}: {code}')
            out[code] += ' / ' + row['name']
        else:
            out[code] = row['name']
    if any(not 0 <= code <= 65535 for code in out): raise ValueError('SIG value exceeds uint16')
    return out
