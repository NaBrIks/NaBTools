# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
import base64
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

path = Path(__file__).resolve().parents[4] / "tools/smarttag_owner_key.py"
spec = importlib.util.spec_from_file_location("owner_key", path)
owner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(owner)


class OwnerKeyTests(unittest.TestCase):
    def test_base64_and_urlsafe_shared_secret(self):
        raw = bytes(range(32))
        for encode in (base64.b64encode, base64.urlsafe_b64encode):
            for value in (encode(raw).decode(), encode(raw).decode().rstrip("=")):
                self.assertEqual(owner.master_secret({"bleD2D": {"encryptionKey": value}}), raw[:16])

    def test_hex_and_16_bytes(self):
        for raw in (bytes(range(16)), bytes(range(32))):
            self.assertEqual(owner.master_secret({"bleD2D": {"encryptionKey": raw.hex()}}), raw[:16])

    def test_location_keys_and_invalid_values_are_rejected(self):
        for document in ({}, {"privateKey": "location-key"}, {"bleD2D": {"encryptionKey": None}}):
            with self.assertRaises(ValueError):
                owner.master_secret(document)
        for value in ("12345", "*invalid*", base64.b64encode(bytes(17)).decode()):
            with self.assertRaises(ValueError):
                owner.master_secret({"bleD2D": {"encryptionKey": value}})

    def test_exclusive_save_contains_no_account_token(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "owner.json"
            device = {"deviceId": "test-id", "bleD2D": {"encryptionKey": bytes(range(32)).hex()}, "unrelated": "secret"}
            with patch("builtins.print"):
                owner.save_credential(device, output)
            saved = json.loads(output.read_text())
            self.assertEqual(saved["master_secret_hex"], bytes(range(16)).hex())
            self.assertEqual(set(saved), {"format", "device_id", "master_secret_hex"})
            with self.assertRaises(FileExistsError):
                owner.save_credential(device, output)

    def test_token_normalization(self):
        for value in ('  abcd  ', 'Bearer abcd', '"Bearer abcd"', "'abcd'"):
            self.assertEqual(owner.normalize_token(value), "abcd")
        for value in ("", "Bearer ", "a b", "пароль"):
            with self.assertRaises(ValueError):
                owner.normalize_token(value)

    def test_redirect_and_unexpected_hosts_never_receive_token(self):
        with self.assertRaises(ValueError):
            owner.NoRedirect().redirect_request(None, None, 302, "", {}, "https://example.com")
        for url in ("https://example.com/v1/devices", "http://api.smartthings.com/v1/devices", "https://api.smartthings.com/v1/users"):
            with self.assertRaises(ValueError):
                owner.api_get(url, "not-a-real-token")

    def test_pagination_selection_and_detail_only(self):
        calls = []
        def get(path):
            calls.append(path)
            if path == "devices":
                return {"items": [{"type": "OTHER", "deviceId": "skip"}], "_links": {"next": {"href": "devices?page=2"}}}
            if path == "devices?page=2":
                return {"items": [{"type": "BLE_D2D", "deviceId": "test-id", "label": "My tag"}]}
            return {"deviceId": "test-id", "bleD2D": {"encryptionKey": bytes(16).hex()}}
        with patch("builtins.input", return_value="1"), patch("builtins.print") as output:
            result = owner.choose_device(get)
        self.assertEqual(calls, ["devices", "devices?page=2", "devices/test-id"])
        self.assertEqual(result["deviceId"], "test-id")
        self.assertEqual(output.call_args_list[0].args, ("1. My tag",))

    def test_private_header_export_is_explicit_and_exclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            credential = Path(directory) / "owner.json"
            header = Path(directory) / "smarttag_owner_secret.hpp"
            credential.write_text(json.dumps({"format": "nabtools.samsung.smarttag.owner.v1", "master_secret_hex": bytes(range(16)).hex()}))
            with patch("builtins.print"):
                owner.firmware_header(credential, header)
            self.assertIn("research_smarttag_master_secret", header.read_text())
            self.assertNotIn("device_id", header.read_text())
            with self.assertRaises(FileExistsError):
                owner.firmware_header(credential, header)


if __name__ == "__main__":
    unittest.main()
