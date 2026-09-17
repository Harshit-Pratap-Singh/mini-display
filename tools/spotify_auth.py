#!/usr/bin/env python3
"""One-time Spotify OAuth, run on this machine, not on the device.

The ESP8266 cannot do this itself. Spotify only accepts plain-HTTP redirect URIs for
loopback IP literals - "http://127.0.0.1:8888/callback" is allowed, "localhost" is
rejected - and a LAN address would need HTTPS, which this board cannot sensibly serve.
So the browser dance happens here and the device only ever holds the refresh token.

Before running, in https://developer.spotify.com/dashboard :
  1. Create an app, note its Client ID and Client Secret.
  2. Add EXACTLY this redirect URI:  http://127.0.0.1:8888/callback
  3. Under User Management, add the Spotify account that will be listening.
     Development Mode apps need the app owner on Premium and allow at most 5 users.

    ./spotify_auth.py --client-id <id>
    ./spotify_auth.py --client-id <id> --save secrets/spotify.json
    ./spotify_auth.py --selftest

The client secret is read from the SPOTIFY_CLIENT_SECRET environment variable, or
prompted for without echo, so it never lands in shell history. Paste the printed
Client ID, Client Secret and Refresh Token into the device's dashboard.
"""

from __future__ import annotations

import argparse
import base64
import getpass
import http.server
import json
import os
import secrets
import sys
import threading
import urllib.error
import urllib.parse
import urllib.request
import webbrowser
from pathlib import Path

AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
# Only what the now-playing screen renders. Asking for more would be rejected by a
# cautious user for no benefit.
SCOPES = "user-read-currently-playing user-read-playback-state"
DEFAULT_PORT = 8888
CALLBACK_PATH = "/callback"


def redirect_uri(port: int) -> str:
    # Must be the IP literal. Spotify rejects "localhost" for plain HTTP.
    return f"http://127.0.0.1:{port}{CALLBACK_PATH}"


def build_auth_url(client_id: str, port: int, state: str) -> str:
    query = urllib.parse.urlencode({
        "client_id": client_id,
        "response_type": "code",
        "redirect_uri": redirect_uri(port),
        "scope": SCOPES,
        "state": state,
        # Force the account chooser, so a second run can pick a different account
        # instead of silently reusing the browser's current session.
        "show_dialog": "true",
    })
    return f"{AUTH_URL}?{query}"


class _Result:
    """Filled in by the callback handler, read by the main thread."""

    def __init__(self) -> None:
        self.code: str | None = None
        self.error: str | None = None
        self.done = threading.Event()


def _make_handler(state: str, result: _Result):
    class CallbackHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self) -> None:  # noqa: N802 - name fixed by BaseHTTPRequestHandler
            parsed = urllib.parse.urlparse(self.path)
            if parsed.path != CALLBACK_PATH:
                self.send_error(404)
                return

            params = urllib.parse.parse_qs(parsed.query)
            returned_state = params.get("state", [""])[0]
            if not secrets.compare_digest(returned_state, state):
                # Someone else's redirect, or a stale tab. Refuse it rather than
                # exchanging a code we did not ask for.
                result.error = "state mismatch - ignore this tab and run the tool again"
            elif "error" in params:
                result.error = params["error"][0]
            elif "code" in params:
                result.code = params["code"][0]
            else:
                result.error = "no code and no error in the redirect"

            body = (
                "<html><body style='font-family:system-ui;padding:3rem'>"
                f"<h2>{'Authorised' if result.code else 'Failed'}</h2>"
                f"<p>{'You can close this tab and return to the terminal.' if result.code else result.error}</p>"
                "</body></html>"
            ).encode()
            self.send_response(200 if result.code else 400)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            result.done.set()

        def log_message(self, *args) -> None:
            pass  # the default logger writes the full callback URL, code included

    return CallbackHandler


def wait_for_code(port: int, state: str, timeout: float) -> str:
    """Serve exactly one callback on loopback and return the authorisation code."""
    result = _Result()
    # Bind to the loopback interface only: nothing on the LAN should reach this.
    server = http.server.HTTPServer(("127.0.0.1", port), _make_handler(state, result))
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        if not result.done.wait(timeout):
            raise SystemExit(f"Timed out after {timeout:.0f}s waiting for the browser redirect.")
    finally:
        server.shutdown()
        server.server_close()

    if result.error:
        raise SystemExit(f"Authorisation failed: {result.error}")
    assert result.code is not None
    return result.code


def exchange_code(client_id: str, client_secret: str, code: str, port: int) -> dict:
    """Swap the one-time code for tokens. Returns the parsed token response."""
    payload = urllib.parse.urlencode({
        "grant_type": "authorization_code",
        "code": code,
        "redirect_uri": redirect_uri(port),
    }).encode()
    basic = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
    request = urllib.request.Request(
        TOKEN_URL,
        data=payload,
        headers={
            "Authorization": f"Basic {basic}",
            "Content-Type": "application/x-www-form-urlencoded",
        },
    )
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.loads(response.read().decode())
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode(errors="replace")
        raise SystemExit(f"Token exchange failed ({exc.code}): {detail}")


def selftest() -> int:
    """Checks the parts that are easy to get subtly wrong, with no network."""
    url = build_auth_url("CLIENT123", DEFAULT_PORT, "STATE456")
    query = urllib.parse.parse_qs(urllib.parse.urlparse(url).query)
    assert query["client_id"] == ["CLIENT123"]
    assert query["response_type"] == ["code"]
    assert query["state"] == ["STATE456"]
    assert query["scope"] == [SCOPES]
    # The single most common cause of INVALID_CLIENT: a redirect URI that does not
    # match the dashboard byte for byte, usually "localhost" instead of the IP.
    assert query["redirect_uri"] == ["http://127.0.0.1:8888/callback"]
    assert "localhost" not in url

    # A mismatched state must be refused rather than exchanged.
    result = _Result()
    handler = _make_handler("EXPECTED", result)
    assert handler is not None
    fake = object.__new__(handler)
    fake.path = "/callback?code=abc&state=WRONG"
    sent = {}
    fake.send_response = lambda c: sent.setdefault("code", c)
    fake.send_header = lambda *a: None
    fake.end_headers = lambda: None
    fake.wfile = type("W", (), {"write": staticmethod(lambda b: None)})()
    fake.do_GET()
    assert result.code is None, "a mismatched state must never yield a code"
    assert "state mismatch" in (result.error or "")
    assert sent["code"] == 400

    print("selftest OK: auth URL shape, loopback redirect URI, state mismatch refused")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client-id", help="Spotify app Client ID")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"loopback port, must match the registered redirect URI (default {DEFAULT_PORT})")
    parser.add_argument("--timeout", type=float, default=300.0,
                        help="seconds to wait for the browser redirect (default 300)")
    parser.add_argument("--save", type=Path,
                        help="also write the credentials to this file (put it under secrets/, which is git-ignored)")
    parser.add_argument("--no-browser", action="store_true",
                        help="print the URL instead of opening a browser")
    parser.add_argument("--selftest", action="store_true", help="run the built-in checks and exit")
    args = parser.parse_args()

    if args.selftest:
        return selftest()
    if not args.client_id:
        parser.error("--client-id is required (or use --selftest)")

    client_secret = os.environ.get("SPOTIFY_CLIENT_SECRET")
    if not client_secret:
        client_secret = getpass.getpass("Spotify Client Secret (not echoed): ").strip()
    if not client_secret:
        raise SystemExit("No client secret given.")

    state = secrets.token_urlsafe(24)
    url = build_auth_url(args.client_id, args.port, state)

    print(f"\nRedirect URI this run expects: {redirect_uri(args.port)}")
    print("It must match the Spotify dashboard exactly, or you get INVALID_CLIENT.\n")
    if args.no_browser:
        print("Open this URL:\n")
        print(url + "\n")
    else:
        print("Opening your browser to approve access...")
        webbrowser.open(url)
        print("If nothing opened, re-run with --no-browser to get the URL.\n")

    code = wait_for_code(args.port, state, args.timeout)
    tokens = exchange_code(args.client_id, client_secret, code, args.port)

    refresh_token = tokens.get("refresh_token")
    if not refresh_token:
        raise SystemExit(f"No refresh token in the response: {tokens}")

    print("\nPaste these into the device dashboard:\n")
    print(f"  Client ID      {args.client_id}")
    print(f"  Client Secret  {client_secret}")
    print(f"  Refresh Token  {refresh_token}")
    print(f"\n(granted scopes: {tokens.get('scope', '?')})")

    if args.save:
        args.save.parent.mkdir(parents=True, exist_ok=True)
        args.save.write_text(json.dumps({
            "clientId": args.client_id,
            "clientSecret": client_secret,
            "refreshToken": refresh_token,
        }, indent=2) + "\n")
        args.save.chmod(0o600)
        print(f"\nSaved to {args.save} (mode 600). Keep it out of git.")

    return 0


if __name__ == "__main__":
    sys.exit(main())
