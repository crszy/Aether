#!/usr/bin/env python3
"""IMAP mail reader for the Zenith shell.

Standard library only (imaplib, email, ssl) -- nothing to install.

Accounts live at ~/.config/zenith/mail.json, owner-readable only. One account:

    {"host": "imap.gmail.com", "port": 993,
     "user": "you@gmail.com", "password": "<app password>", "mailbox": "INBOX"}

or several:

    {"accounts": [ {...}, {...} ]}

Gmail needs an App Password (2-step verification on); the account password will
not authenticate over IMAP. https://myaccount.google.com/apppasswords

Every successful list is cached under ~/.cache/zenith/mail/, so the UI can show
the previous messages instantly on start instead of an empty box, then refresh.

Commands:
    accounts                 configured accounts
    status  [--account A]    connection state + unread count
    list N  [--account A]    newest N messages (also refreshes the cache)
    cached  [--account A]    last cached list, no network
    body   <uid>             full text of one message
    read   <uid>             mark seen
    delete <uid>             move to Trash
"""

import email
import email.policy
import email.utils
import html
import html.parser
import imaplib
import json
import os
import re
import socket
import ssl
import sys
from email.header import decode_header, make_header

HOME = os.path.expanduser("~")
CONFIG = os.environ.get(
    "ZENITH_MAIL_CONFIG", os.path.join(HOME, ".config", "zenith", "mail.json"))
CACHE_DIR = os.environ.get(
    "ZENITH_MAIL_CACHE",
    os.path.join(os.environ.get("XDG_CACHE_HOME", os.path.join(HOME, ".cache")),
                 "zenith", "mail"))
TIMEOUT = 20


def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def normalise(cfg):
    cfg = dict(cfg)
    cfg["password"] = "".join(str(cfg.get("password", "")).split())
    cfg.setdefault("host", "imap.gmail.com")
    cfg.setdefault("port", 993)
    cfg.setdefault("mailbox", "INBOX")
    return cfg


def load_accounts():
    """Every configured account. Accepts the old single-account file as well."""
    try:
        with open(CONFIG, encoding="utf-8") as f:
            raw = json.load(f)
    except FileNotFoundError:
        return [], "No mail account configured."
    except Exception as e:
        return [], "Config is not valid JSON: %s" % e

    entries = raw.get("accounts") if isinstance(raw, dict) else None
    if entries is None:
        entries = [raw] if isinstance(raw, dict) else []

    out = []
    for entry in entries:
        if isinstance(entry, dict) and entry.get("user") and entry.get("password"):
            out.append(normalise(entry))
    if not out:
        return [], "Config has no usable account."
    return out, None


def pick(accounts, selector):
    """Select by index or by (partial) address; first account by default."""
    if selector in (None, ""):
        return accounts[0]
    if str(selector).isdigit():
        i = int(selector)
        return accounts[i] if 0 <= i < len(accounts) else accounts[0]
    for a in accounts:
        if str(selector).lower() in a["user"].lower():
            return a
    return accounts[0]


def cache_path(cfg):
    safe = re.sub(r"[^A-Za-z0-9_.@-]", "_", cfg["user"])
    return os.path.join(CACHE_DIR, safe + ".json")


def write_cache(cfg, payload):
    """Best effort -- a cache failure must never break a successful fetch."""
    try:
        os.makedirs(CACHE_DIR, exist_ok=True)
        tmp = cache_path(cfg) + ".tmp"
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(payload, f)
        os.replace(tmp, cache_path(cfg))
    except Exception:
        pass


def read_cache(cfg):
    try:
        with open(cache_path(cfg), encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def decode(raw):
    """MIME-encoded headers -> readable text, without ever raising."""
    if not raw:
        return ""
    try:
        return str(make_header(decode_header(raw))).strip()
    except Exception:
        return str(raw).strip()


class _HtmlText(html.parser.HTMLParser):
    """Bare-minimum HTML to text.

    Plenty of mail is HTML-only. Rendering it properly is out of scope for a
    shell panel, but showing raw tags is useless, so script/style are dropped
    and block elements become line breaks.
    """

    SKIP = {"script", "style", "head", "title"}
    BREAK = {"p", "div", "br", "tr", "li", "h1", "h2", "h3", "h4", "table"}

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []
        self._skip = 0

    def handle_starttag(self, tag, attrs):
        if tag in self.SKIP:
            self._skip += 1
        elif tag in self.BREAK:
            self.parts.append("\n")

    def handle_endtag(self, tag):
        if tag in self.SKIP and self._skip:
            self._skip -= 1
        elif tag in self.BREAK:
            self.parts.append("\n")

    def handle_data(self, data):
        if not self._skip:
            self.parts.append(data)

    def text(self):
        out = "".join(self.parts)
        lines = [ln.strip() for ln in out.splitlines()]
        cleaned, blank = [], 0
        for ln in lines:
            if ln:
                cleaned.append(ln)
                blank = 0
            else:
                blank += 1
                if blank < 2:
                    cleaned.append("")
        return "\n".join(cleaned).strip()


def part_text(part):
    """Decoded text of one MIME part, never raising on a bad charset."""
    payload = part.get_payload(decode=True)
    if payload is None:
        return ""
    charset = part.get_content_charset() or "utf-8"
    try:
        return payload.decode(charset, errors="replace")
    except (LookupError, TypeError):
        return payload.decode("utf-8", errors="replace")


def extract_body(msg, limit=120000):
    """(text, was_html, [attachment names]) for a parsed message."""
    plain, htmls, attachments = [], [], []

    for part in msg.walk():
        if part.get_content_maintype() == "multipart":
            continue
        disp = str(part.get("Content-Disposition") or "")
        filename = part.get_filename()
        if filename or "attachment" in disp.lower():
            if filename:
                attachments.append(decode(filename))
            continue

        ctype = part.get_content_type()
        if ctype == "text/plain":
            plain.append(part_text(part))
        elif ctype == "text/html":
            htmls.append(part_text(part))

    was_html = False
    if any(p.strip() for p in plain):
        body = "\n".join(plain)
    elif htmls:
        parser = _HtmlText()
        try:
            parser.feed("\n".join(htmls))
            body = parser.text()
        except Exception:
            body = "\n".join(htmls)
        was_html = True
    else:
        body = ""

    body = body.replace("\r\n", "\n").replace("\r", "\n").strip()
    if len(body) > limit:
        body = body[:limit] + "\n\n[... truncated]"
    return body, was_html, attachments


def connect(cfg):
    ctx = ssl.create_default_context()
    conn = imaplib.IMAP4_SSL(cfg["host"], int(cfg["port"]),
                             ssl_context=ctx, timeout=TIMEOUT)
    conn.login(cfg["user"], cfg["password"])
    return conn


def trash_folder(conn):
    """The server's Trash, via the SPECIAL-USE flag when it advertises one.

    Gmail calls it "[Gmail]/Trash", Fastmail and others just "Trash", and a
    localised server calls it something else entirely -- so ask rather than
    guess, and only fall back to the common names.
    """
    try:
        typ, boxes = conn.list()
        if typ == "OK":
            for raw in boxes or []:
                line = raw.decode("utf-8", "ignore") if isinstance(raw, bytes) else str(raw)
                if "\\Trash" in line:
                    name = line.split(' "')[-1].strip().strip('"')
                    if name:
                        return name
    except Exception:
        pass
    return None


def fetch(cfg, limit):
    conn = connect(cfg)
    try:
        conn.select(cfg["mailbox"], readonly=True)

        # UIDs, not sequence numbers: sequence numbers shift as the mailbox
        # changes, so acting on one later could hit the wrong message.
        typ, data = conn.uid("search", None, "ALL")
        if typ != "OK" or not data or not data[0]:
            return [], 0
        uids = data[0].split()[-limit:]
        uids.reverse()

        typ, unseen = conn.uid("search", None, "UNSEEN")
        unread = len(unseen[0].split()) if typ == "OK" and unseen and unseen[0] else 0

        items = []
        for uid in uids:
            typ, msg_data = conn.uid(
                "fetch", uid,
                "(FLAGS BODY.PEEK[HEADER.FIELDS (FROM SUBJECT DATE)])")
            if typ != "OK" or not msg_data:
                continue

            flags, raw = b"", b""
            for part in msg_data:
                if isinstance(part, tuple):
                    flags = flags or part[0]
                    raw = part[1]
                elif isinstance(part, bytes):
                    flags += part

            msg = email.message_from_bytes(raw)
            from_raw = decode(msg.get("From"))
            name, addr = email.utils.parseaddr(from_raw)

            try:
                ts = int(email.utils.parsedate_to_datetime(msg.get("Date")).timestamp())
            except Exception:
                ts = 0

            items.append({
                "uid": uid.decode("ascii", "ignore"),
                "from": name or addr or from_raw or "(unknown)",
                "addr": addr,
                "subject": decode(msg.get("Subject")) or "(no subject)",
                "ts": ts,
                "unread": b"\\Seen" not in flags,
            })
        return items, unread
    finally:
        try:
            conn.logout()
        except Exception:
            pass


def classify(e):
    """Split "wrong password" from "no network" -- only one is actionable."""
    if isinstance(e, imaplib.IMAP4.error):
        text = str(e).lower()
        if "application-specific" in text or "app password" in text:
            return "auth", ("Gmail needs an App Password, not your account "
                            "password. Create one at "
                            "myaccount.google.com/apppasswords")
        return "auth", "Mail server rejected these credentials."
    if isinstance(e, (socket.gaierror, socket.timeout, TimeoutError, OSError, ssl.SSLError)):
        return "offline", "Could not reach the mail server."
    return "error", str(e)


def main():
    argv = sys.argv[1:]
    selector = None
    if "--account" in argv:
        i = argv.index("--account")
        if i + 1 < len(argv):
            selector = argv[i + 1]
        del argv[i:i + 2]

    cmd = argv[0] if argv else "status"
    accounts, err = load_accounts()

    if not accounts:
        emit({"type": "error", "message": err, "needs_config": True})
        return

    if cmd == "accounts":
        emit({"type": "accounts",
              "accounts": [{"user": a["user"], "host": a["host"]} for a in accounts]})
        return

    cfg = pick(accounts, selector)

    # Served from disk, so the UI has something to show immediately on start.
    if cmd == "cached":
        cached = read_cache(cfg)
        if cached:
            cached["type"] = "list"
            cached["cached"] = True
            emit(cached)
        else:
            emit({"type": "list", "cached": True, "user": cfg["user"],
                  "unread": 0, "messages": []})
        return

    try:
        if cmd == "status":
            items, unread = fetch(cfg, 1)
            emit({"type": "status", "connected": True,
                  "unread": unread, "user": cfg["user"]})

        elif cmd == "list":
            limit = 20
            if len(argv) > 1:
                try:
                    limit = max(1, min(100, int(argv[1])))
                except ValueError:
                    pass
            items, unread = fetch(cfg, limit)
            payload = {"unread": unread, "user": cfg["user"], "messages": items}
            write_cache(cfg, payload)
            out = dict(payload)
            out["type"] = "list"
            out["cached"] = False
            emit(out)

        elif cmd == "body":
            if len(argv) < 2:
                emit({"type": "error", "message": "body needs a uid"})
                return
            uid = argv[1]
            conn = connect(cfg)
            try:
                conn.select(cfg["mailbox"], readonly=True)
                typ, data = conn.uid("fetch", uid, "(BODY.PEEK[])")
                raw = b""
                for part in data or []:
                    if isinstance(part, tuple):
                        raw = part[1]
                if typ != "OK" or not raw:
                    emit({"type": "error", "message": "Message not found."})
                    return
                msg = email.message_from_bytes(raw)
                body, was_html, attachments = extract_body(msg)
                emit({
                    "type": "body", "uid": uid,
                    "from": decode(msg.get("From")),
                    "to": decode(msg.get("To")),
                    "subject": decode(msg.get("Subject")) or "(no subject)",
                    "date": decode(msg.get("Date")),
                    "message_id": (msg.get("Message-Id") or "").strip().strip("<>"),
                    "html": was_html, "attachments": attachments, "body": body,
                })
            finally:
                try:
                    conn.logout()
                except Exception:
                    pass

        elif cmd == "read":
            if len(argv) < 2:
                emit({"type": "error", "message": "read needs a uid"})
                return
            conn = connect(cfg)
            try:
                conn.select(cfg["mailbox"])
                conn.uid("store", argv[1], "+FLAGS", "(\\Seen)")
            finally:
                try:
                    conn.logout()
                except Exception:
                    pass
            emit({"type": "read_done", "uid": argv[1]})

        elif cmd == "delete":
            if len(argv) < 2:
                emit({"type": "error", "message": "delete needs a uid"})
                return
            uid = argv[1]
            conn = connect(cfg)
            try:
                conn.select(cfg["mailbox"])
                trash = trash_folder(conn)
                # Mailbox names go on the wire as quoted strings. Gmail's is
                # "[Gmail]/Bin" on a UK account -- brackets and a space -- and
                # sending that bare malforms the command, which surfaced as a
                # dropped connection reported as "offline" even though the
                # very same session had just listed the inbox.
                quoted = '"%s"' % trash.replace('"', '\\"') if trash else None
                moved = False

                # MOVE (RFC 6851) when the server has it -- one atomic step,
                # and it lands in Trash rather than vanishing.
                if trash:
                    try:
                        typ, _ = conn.uid("move", uid, quoted)
                        moved = typ == "OK"
                    except Exception:
                        moved = False
                    if not moved:
                        try:
                            typ, _ = conn.uid("copy", uid, quoted)
                            if typ == "OK":
                                conn.uid("store", uid, "+FLAGS", "(\\Deleted)")
                                conn.expunge()
                                moved = True
                        except Exception:
                            moved = False

                if not moved:
                    # No Trash advertised: flag and expunge is all that is left.
                    conn.uid("store", uid, "+FLAGS", "(\\Deleted)")
                    conn.expunge()

                # Keep the cache honest so a restart does not resurrect it.
                cached = read_cache(cfg)
                if cached and isinstance(cached.get("messages"), list):
                    cached["messages"] = [m for m in cached["messages"]
                                          if str(m.get("uid")) != str(uid)]
                    write_cache(cfg, cached)
            finally:
                try:
                    conn.logout()
                except Exception:
                    pass
            emit({"type": "delete_done", "uid": uid, "trash": trash or ""})

        else:
            emit({"type": "error", "message": "unknown command: %s" % cmd})

    except Exception as e:  # noqa: BLE001 - a mail failure must not kill the tab
        kind, message = classify(e)
        emit({"type": "error", "message": message,
              "auth": kind == "auth", "offline": kind == "offline"})


if __name__ == "__main__":
    main()
