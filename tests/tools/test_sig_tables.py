# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
"""Offline validation of Bluetooth SIG name-table parsing."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from sig_tables import TABLES, read_table


class SigTablesTests(unittest.TestCase):
    def read(self, table, text):
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            target = base / TABLES[table][0]
            target.parent.mkdir(parents=True)
            target.write_text(text, encoding='utf8')
            return read_table(base, table)

    def test_contextual_ad_name_is_preserved(self):
        rows = self.read('ad_type', 'ad_types:\n'
                         '  - {value: 16, name: Device ID}\n'
                         '  - {value: 16, name: Security Manager TK Value}\n')
        self.assertEqual(rows[16], 'Device ID / Security Manager TK Value')

    def test_duplicate_and_out_of_range_uuid_are_rejected(self):
        for rows in ('  - {uuid: 1, name: A}\n  - {uuid: 1, name: B}\n',
                     '  - {uuid: 65536, name: A}\n',
                     '  - {uuid: -1, name: A}\n'):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                self.read('service', 'uuids:\n' + rows)

    def test_appearance_bit_fields_and_names(self):
        rows = self.read('appearance', 'appearance_values:\n'
                         '  - category: 13\n    name: Heart Rate Sensor\n'
                         '    subcategory:\n      - {value: 1, name: Heart Rate Belt}\n')
        self.assertEqual(rows, {832: 'Heart Rate Sensor',
                                833: 'Heart Rate Sensor: Heart Rate Belt'})

    def test_appearance_collisions_and_invalid_bit_fields_are_rejected(self):
        for rows in ('  - {category: 1024, name: A}\n',
                     '  - {category: -1, name: A}\n',
                     '  - {category: 1, name: A}\n  - {category: 1, name: B}\n',
                     *('  - category: 1\n    name: A\n    subcategory:\n'
                       f'      - {{value: {value}, name: B}}\n' for value in (0, -1, 64)),
                     '  - category: 1\n    name: A\n    subcategory:\n'
                     '      - {value: 1, name: B}\n      - {value: 1, name: C}\n'):
            with self.subTest(rows=rows), self.assertRaises(ValueError):
                self.read('appearance', 'appearance_values:\n' + rows)


if __name__ == '__main__':
    unittest.main()
