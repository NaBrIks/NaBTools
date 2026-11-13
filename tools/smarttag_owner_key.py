#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleg Kozhevnikov
"""Read an owner's SmartThings device metadata; never register or modify a tag.

The access token is entered without echo and is never written to disk. Only the
selected tag's credential is saved. Keep the output outside version control.
Protocol references: docs/readme_smarttag_owner_ru.md.
"""
import argparse
import base64
import getpass
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

API = "https://api.smartthings.com/v1/"


def normalize_token(value):
    value = value.strip().strip('"\'').strip()
    if value.lower().startswith("bearer "):
        value = value[7:].strip()
    if not value or value.lower() == "bearer" or len(value) > 4096 or any(c.isspace() or not c.isascii() for c in value):
        raise ValueError("Invalid token input. Paste the generated token value, without its name.")
    return value


def master_secret(device):
    """Decode BLE shared secret, not the EC key used for location encryption."""
    ble = device.get("bleD2D") or {}
    value = ble.get("encryptionKey")
    if not isinstance(value, str) or not value:
        raise ValueError("bleD2D.encryptionKey is absent. The API did not disclose a BLE owner key.")
    if re.fullmatch(r"[0-9a-fA-F]{32}|[0-9a-fA-F]{64}", value):
        raw = bytes.fromhex(value)
    else:
        if not re.fullmatch(r"[A-Za-z0-9_+/\-]+={0,2}", value):
            raise ValueError("Unsupported BLE encryptionKey encoding.")
        try:
            raw = base64.b64decode(value + "=" * (-len(value) % 4), altchars=b"-_", validate=True)
        except ValueError:
            raise ValueError("Invalid BLE encryptionKey encoding.") from None
    if len(raw) not in (16, 32):
        raise ValueError("Expected a 16-byte master secret or 32-byte BLE shared secret.")
    return raw[:16]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError("API redirect refused; token was not forwarded.")


def api_get(path, token):
    url = urllib.parse.urljoin(API, path)
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or parsed.netloc != "api.smartthings.com" or not parsed.path.startswith("/v1/devices"):
        raise ValueError("Unexpected API URL; request refused.")
    request = urllib.request.Request(url, headers={"Authorization": "Bearer " + token,
        "Accept": "application/json"})
    try:
        with urllib.request.build_opener(NoRedirect).open(request, timeout=25) as response:
            # Bound metadata responses; never print server bodies or credentials.
            data = response.read(2 * 1024 * 1024 + 1)
        if len(data) > 2 * 1024 * 1024:
            raise ValueError("API metadata response too large.")
        return json.loads(data)
    except urllib.error.HTTPError as error:
        # Only safe diagnostic fields; never dump request headers or the body.
        diagnostic = f"response={error.headers.get_content_type()}"
        try:
            body = json.loads(error.read(8192))
            code = (body.get("error") or {}).get("code") if isinstance(body.get("error"), dict) else body.get("error")
            if isinstance(code, str) and re.fullmatch(r"[A-Za-z0-9_.-]{1,80}", code):
                diagnostic += ", code=" + code
        except (ValueError, AttributeError):
            pass
        if error.code == 401:
            raise ValueError("SmartThings HTTP 401: access token rejected before reading devices. "
                "Use the generated token VALUE (not the token name or ID from the list); "
                "new PATs expire after 24 hours. No tag key was requested or changed. " + diagnostic) from None
        raise ValueError(f"SmartThings HTTP {error.code}. Check read-devices permission and account access.") from None
    except urllib.error.URLError:
        raise ValueError("SmartThings connection failed. Check network and proxy settings.") from None


def cli_get(path):
    # Official CLI owns OAuth/browser login and its standard credential store.
    # Its response is captured in memory; never print complete device metadata.
    entry = Path(__file__).resolve().parents[1] / "local/smartthings-cli/node_modules/@smartthings/cli/dist/src/run.js"
    node = shutil.which("node")
    if not node or not entry.is_file():
        raise ValueError("Install the official @smartthings/cli under local/smartthings-cli first (see owner guide).")
    # Local optional adapter: avoid the CLI's open() package launching PowerShell
    # on Windows. The official OAuth flow is unchanged; the owner opens its URL.
    login_module = entry.parent / "lib/login-authenticator.js"
    source = login_module.read_text(encoding="utf-8")
    if "NABTOOLS_CLI_LOGIN_FILE" not in source:
        original = "await open(`http://localhost:${port}/start`);"
        if source.count(original) != 1:
            raise ValueError("Unsupported CLI browser-launch implementation. Manual-login adapter was not applied.")
        replacement = "if (process.env.NABTOOLS_CLI_LOGIN_FILE) { const fs = await import('node:fs/promises'); await fs.writeFile(process.env.NABTOOLS_CLI_LOGIN_FILE, `http://localhost:${port}/start`); } else { " + original + " }"
        login_module.write_text(source.replace(original, replacement), encoding="utf-8")
    match = re.fullmatch(r"devices(?:/([A-Za-z0-9-]{1,64}))?", path)
    if not match:
        raise ValueError("Unexpected CLI request.")
    command = [node, str(entry), "devices"]
    if match[1]:
        command.append(match[1])
    command.extend(["--json", "--profile", "nabtools-owner"])
    env = os.environ.copy()
    env.pop("SMARTTHINGS_TOKEN", None)
    env.pop("SMARTTHINGS_DEBUG", None)
    print("Official SmartThings CLI: waiting for login / device metadata...", flush=True)
    with tempfile.TemporaryDirectory(prefix="nabtools-cli-login-") as directory, tempfile.TemporaryFile() as output, tempfile.TemporaryFile() as errors:
        login_file = Path(directory) / "login-url.txt"
        env["NABTOOLS_CLI_LOGIN_FILE"] = str(login_file)
        process = subprocess.Popen(command, stdout=output, stderr=errors, env=env)
        deadline = time.monotonic() + 660
        shown = False
        try:
            while process.poll() is None:
                if not shown and login_file.exists():
                    url = login_file.read_text(encoding="utf-8")
                    parsed = urllib.parse.urlsplit(url)
                    if parsed.scheme != "http" or parsed.hostname != "localhost" or parsed.path != "/start" or not parsed.port or parsed.port == 443:
                        raise ValueError("Unexpected CLI callback URL.")
                    print("Open this URL manually in your browser: " + url, flush=True)
                    shown = True
                if time.monotonic() > deadline:
                    raise ValueError("SmartThings browser login timed out.")
                time.sleep(0.2)
            if process.returncode:
                raise ValueError("Official SmartThings CLI failed. No token or device metadata printed. Check the browser login result.")
            output.seek(0)
            try:
                data = json.load(output)
            except ValueError:
                raise ValueError("CLI did not return JSON device metadata.") from None
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
    return {"items": data} if isinstance(data, list) else data


def choose_device(get):
    devices = []
    path = "devices"
    visited = set()
    while path:
        if path in visited or len(visited) >= 100:
            raise ValueError("Invalid API pagination.")
        visited.add(path)
        page = get(path)
        devices.extend(d for d in page.get("items", []) if d.get("type") == "BLE_D2D" or d.get("bleD2D"))
        path = (page.get("_links", {}).get("next") or {}).get("href")
    if not devices:
        raise ValueError("No BLE_D2D devices visible to this account/token.")
    for index, device in enumerate(devices, 1):
        # Whitelist presentation fields instead of dumping API metadata.
        label = str(device.get("label") or device.get("name") or "BLE device")
        label = "".join(c for c in label if c.isprintable())[:100]
        print(f"{index}. {label}")
    choice = int(input("Tag number: "))
    if not 1 <= choice <= len(devices):
        raise ValueError("Invalid device selection.")
    device_id = devices[choice - 1].get("deviceId", "")
    if not re.fullmatch(r"[A-Za-z0-9-]{1,64}", device_id):
        raise ValueError("Invalid device identifier.")
    return get("devices/" + device_id)


def save_credential(device, output):
    secret = master_secret(device)
    credential = {"format": "nabtools.samsung.smarttag.owner.v1", "device_id": device.get("deviceId"),
        "master_secret_hex": secret.hex()}
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive create prevents accidentally replacing another tag's credential.
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        json.dump(credential, stream, indent=2)
        stream.write("\n")
    print("Owner credential saved locally. No token saved. BLE authentication still needs verification.")


def firmware_header(credential_path, output):
    data = json.loads(Path(credential_path).read_text(encoding="utf-8-sig"))
    value = data.get("master_secret_hex", "")
    if data.get("format") != "nabtools.samsung.smarttag.owner.v1" or not re.fullmatch(r"[0-9a-fA-F]{32}", value):
        raise ValueError("Invalid owner credential document.")
    # No account token/device metadata is put in the firmware. Development only:
    # replace this provider with application-owned secure storage in products.
    raw = bytes.fromhex(value)
    source = "#pragma once\n#include <array>\n#include <cstdint>\n"
    source += "// PRIVATE generated development credential. Never commit or distribute.\n"
    source += "inline constexpr std::array<uint8_t,16> research_smarttag_master_secret{" + ",".join(f"0x{b:02x}" for b in raw) + "};\n"
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    fd = os.open(output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as stream:
        stream.write(source)
    print("Private development firmware header created. Keep firmware binaries private too.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--response", type=Path, help="Import an existing GET /devices/{id} JSON response offline")
    parser.add_argument("--cli", action="store_true", help="Use official SmartThings CLI browser OAuth instead of a pasted PAT")
    parser.add_argument("--credential", type=Path, help="Existing local credential to use with --firmware-header (no account API request)")
    parser.add_argument("--firmware-header", type=Path, help="Create a PRIVATE development header from the local credential")
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parents[1] / "local/smarttag-owner.json")
    args = parser.parse_args()
    try:
        if args.credential:
            if not args.firmware_header:
                raise ValueError("--credential requires --firmware-header.")
            firmware_header(args.credential, args.firmware_header)
            return 0
        if args.output.exists():
            raise ValueError("Output already exists; choose another --output path.")
        if args.response:
            device = json.loads(args.response.read_text(encoding="utf-8-sig"))
        elif args.cli:
            device = choose_device(cli_get)
        else:
            token = normalize_token(getpass.getpass("SmartThings token VALUE (hidden, read devices only): "))
            device = choose_device(lambda path: api_get(path, token))
        save_credential(device, args.output)
        if args.firmware_header:
            firmware_header(args.output, args.firmware_header)
    except (ValueError, OSError, EOFError, KeyboardInterrupt):
        # Avoid traceback/server payloads that could contain secrets.
        error = sys.exc_info()[1]
        if isinstance(error, ValueError):
            print(str(error), file=sys.stderr)
        else:
            print("Cancelled or local file operation failed. No credentials printed.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
