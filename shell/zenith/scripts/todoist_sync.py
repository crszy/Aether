#!/usr/bin/env python3
"""Todoist sync for the Zenith todo list.

Offline-first. The local file is always the thing the UI reads and writes; this
script reconciles it with Todoist whenever the network allows, and is a no-op
that leaves local data untouched when it does not.

Model
-----
A local "tab" maps to a Todoist project of the same name. A local task maps to a
Todoist task, correlated by the ``todoist_id`` stored alongside it. Tasks created
while offline simply have no ``todoist_id`` yet, which is exactly the signal that
they still need pushing.

Conflict rule
-------------
Todoist wins. For any task that exists on both sides, the server's content and
completion state overwrite the local copy. This is the rule you want when the
same list is also open on a phone: the shell never silently reverts a change
made in the app. Purely-local work is never lost -- tasks with no ``todoist_id``
are pushed up, and deletions/completions made offline are queued and replayed.

API
---
Todoist API v1 (``https://api.todoist.com/api/v1``). The older ``rest/v2`` and
``sync/v9`` surfaces both return 410 Gone -- verified by probing them directly --
so anything written against those, which is most published example code, fails
on every call.

Usage:
    todoist_sync.py status     # connection + pending-change summary
    todoist_sync.py sync       # reconcile local <-> Todoist
"""

import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

# Overridable so the merge logic can be exercised against a mock API and
# throwaway files instead of the real service and the user's real task list.
API_BASE = os.environ.get("ZENITH_TODOIST_API", "https://api.todoist.com/api/v1")
TIMEOUT = 15

HOME = os.path.expanduser("~")
TOKEN_FILE = os.environ.get(
    "ZENITH_TODOIST_TOKEN_FILE",
    os.path.join(HOME, ".config", "zenith", "todoist.json"))
TASKS_FILE = os.environ.get(
    "ZENITH_TODO_FILE",
    os.path.join(HOME, "Documents", "Task", "todo.json"))


# --------------------------------------------------------------------------
# Local store
# --------------------------------------------------------------------------

def emit(obj):
    sys.stdout.write(json.dumps(obj) + "\n")
    sys.stdout.flush()


def read_token():
    try:
        with open(TOKEN_FILE, encoding="utf-8") as f:
            return (json.load(f).get("token") or "").strip()
    except Exception:
        return ""


def read_local():
    """Return the tab list. Shape: [{name, tasks: [...]}, ...]."""
    try:
        with open(TASKS_FILE, encoding="utf-8") as f:
            data = json.load(f)
    except Exception:
        return []
    if not isinstance(data, list):
        return []

    for tab in data:
        tab.setdefault("name", "Tasks")
        tab.setdefault("tasks", [])
        for t in tab["tasks"]:
            t.setdefault("task", "")
            t.setdefault("completed", False)
            # todoist_id is absent for anything created offline.
            # deleted marks a local removal that still needs pushing.
            t.setdefault("deleted", False)
    return data


def write_local(tabs):
    """Write atomically so a crash mid-write cannot truncate the task list."""
    os.makedirs(os.path.dirname(TASKS_FILE), exist_ok=True)
    tmp = TASKS_FILE + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(tabs, f, indent=2)
    os.replace(tmp, TASKS_FILE)


# --------------------------------------------------------------------------
# API
# --------------------------------------------------------------------------

class ApiError(Exception):
    def __init__(self, message, offline=False, auth=False):
        super().__init__(message)
        self.offline = offline
        self.auth = auth


def api(token, path, method="GET", payload=None, params=None):
    url = API_BASE + path
    if params:
        url += "?" + urllib.parse.urlencode(params)

    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(
        url,
        data=data,
        method=method,
        headers={
            "Authorization": "Bearer " + token,
            "Content-Type": "application/json",
        },
    )
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT) as resp:
            body = resp.read().decode("utf-8", errors="ignore")
            return json.loads(body) if body.strip() else None
    except urllib.error.HTTPError as e:
        if e.code in (401, 403):
            raise ApiError("Todoist rejected the token.", auth=True)
        if e.code == 410:
            raise ApiError("This Todoist API version was retired.")
        if e.code == 429:
            raise ApiError("Rate limited by Todoist; will retry later.", offline=True)
        detail = e.read().decode("utf-8", errors="ignore")[:200]
        raise ApiError("Todoist HTTP %s: %s" % (e.code, detail))
    except (urllib.error.URLError, TimeoutError, OSError) as e:
        # No network. Not an error the user needs to act on -- work stays local.
        raise ApiError("Could not reach Todoist (%s)." % e, offline=True)


def paged(token, path, key):
    """API v1 paginates with a cursor; walk it to completion."""
    out, cursor = [], None
    while True:
        params = {"limit": 200}
        if cursor:
            params["cursor"] = cursor
        page = api(token, path, params=params)
        if isinstance(page, dict):
            out.extend(page.get(key) or page.get("results") or [])
            cursor = page.get("next_cursor")
            if not cursor:
                break
        elif isinstance(page, list):
            out.extend(page)
            break
        else:
            break
    return out


def fetch_projects(token):
    return paged(token, "/projects", "results")


def fetch_tasks(token):
    return paged(token, "/tasks", "results")


# --------------------------------------------------------------------------
# Sync
# --------------------------------------------------------------------------

def pending_count(tabs):
    """Local changes not yet reflected on the server."""
    n = 0
    for tab in tabs:
        for t in tab["tasks"]:
            if t.get("deleted"):
                n += 1
            elif not t.get("todoist_id"):
                n += 1
            elif t.get("dirty"):
                n += 1
    return n


def sync(token):
    tabs = read_local()

    projects = fetch_projects(token)

    proj_by_name = {p["name"]: p for p in projects if p.get("name")}
    proj_by_id = {p["id"]: p for p in projects if p.get("id")}

    pushed = updated = pulled = removed = 0

    # --- 1. Push local-only work up -------------------------------------
    for tab in tabs:
        project = proj_by_name.get(tab["name"])

        surviving = []
        for t in tab["tasks"]:
            tid = t.get("todoist_id")

            # Deleted offline -> delete on the server, then drop locally.
            if t.get("deleted"):
                if tid:
                    try:
                        api(token, "/tasks/%s" % tid, method="DELETE")
                        removed += 1
                    except ApiError as e:
                        if e.offline:
                            raise
                        # Already gone server-side; dropping locally is correct.
                continue

            # Created offline -> needs a project to live in.
            if not tid:
                if project is None:
                    project = api(token, "/projects", method="POST",
                                  payload={"name": tab["name"]})
                    proj_by_name[tab["name"]] = project
                    proj_by_id[project["id"]] = project
                created = api(token, "/tasks", method="POST", payload={
                    "content": t["task"],
                    "project_id": project["id"],
                })
                t["todoist_id"] = created["id"]
                if t.get("completed"):
                    api(token, "/tasks/%s/close" % created["id"], method="POST")
                t.pop("dirty", None)
                pushed += 1

            # Edited offline -> push the edit before the server copy is read
            # back, so the local change is not the one that loses.
            elif t.get("dirty"):
                api(token, "/tasks/%s" % tid, method="POST",
                    payload={"content": t["task"]})
                path = "/tasks/%s/%s" % (tid, "close" if t.get("completed") else "reopen")
                api(token, path, method="POST")
                t.pop("dirty", None)
                updated += 1

            surviving.append(t)
        tab["tasks"] = surviving

    # --- 2. Pull the server's view down (Todoist wins) ------------------
    remote_tasks = fetch_tasks(token)
    remote_by_id = {str(t["id"]): t for t in remote_tasks}

    # A tab per project that actually has tasks, in Todoist's own project
    # order. Empty projects are skipped: an account with a dozen of them would
    # otherwise fill a small shell menu with blank tabs. Tabs created locally
    # are never touched here, empty or not.
    active_project_ids = {rt.get("project_id") for rt in remote_tasks}
    tabs_by_name = {tab["name"]: tab for tab in tabs}
    for p in projects:
        if p["name"] not in tabs_by_name and p.get("id") in active_project_ids:
            new_tab = {"name": p["name"], "tasks": []}
            tabs.append(new_tab)
            tabs_by_name[p["name"]] = new_tab

    seen_ids = set()
    for tab in tabs:
        kept = []
        for t in tab["tasks"]:
            tid = str(t.get("todoist_id") or "")
            if not tid:
                kept.append(t)
                continue
            remote = remote_by_id.get(tid)
            if remote is None:
                # /tasks returns active tasks only, so "absent" means completed
                # *or* deleted. One already ticked on this side is the former --
                # keep it, struck through, rather than letting the checkbox
                # behave like a delete button.
                if t.get("completed"):
                    kept.append(t)
                else:
                    removed += 1
                continue
            # Server overwrites local -- the stated conflict rule.
            t["task"] = remote.get("content", t["task"])
            t["completed"] = bool(remote.get("is_completed", t.get("completed")))
            t.pop("dirty", None)
            seen_ids.add(tid)
            kept.append(t)
        tab["tasks"] = kept

    # Tasks that exist in Todoist but not locally.
    for rt in remote_tasks:
        rid = str(rt["id"])
        if rid in seen_ids:
            continue
        project = proj_by_id.get(rt.get("project_id"))
        tab_name = project["name"] if project else "Inbox"
        tab = tabs_by_name.get(tab_name)
        if tab is None:
            tab = {"name": tab_name, "tasks": []}
            tabs.append(tab)
            tabs_by_name[tab_name] = tab
        tab["tasks"].append({
            "task": rt.get("content", ""),
            "completed": bool(rt.get("is_completed", False)),
            "todoist_id": rid,
            "deleted": False,
        })
        pulled += 1

    if not tabs:
        tabs = [{"name": "Tasks", "tasks": []}]

    write_local(tabs)
    return {
        "type": "sync_done",
        "pushed": pushed,
        "updated": updated,
        "pulled": pulled,
        "removed": removed,
        "tabs": len(tabs),
        "at": int(time.time()),
    }


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "status"
    token = read_token()

    if cmd == "status":
        tabs = read_local()
        emit({
            "type": "status",
            "has_token": bool(token),
            "pending": pending_count(tabs),
            "tabs": len(tabs),
        })
        return

    if cmd != "sync":
        emit({"type": "error", "message": "unknown command: %s" % cmd})
        return

    if not token:
        emit({"type": "error", "message": "No Todoist API token configured.",
              "needs_token": True})
        return

    try:
        emit(sync(token))
    except ApiError as e:
        # Offline is an expected state, not a failure: the local file is
        # untouched and the next run will reconcile.
        emit({
            "type": "error",
            "message": str(e),
            "offline": e.offline,
            "auth": e.auth,
        })
    except Exception as e:  # noqa: BLE001 - never let a crash corrupt the list
        emit({"type": "error", "message": "Sync failed: %s" % e})


if __name__ == "__main__":
    main()
