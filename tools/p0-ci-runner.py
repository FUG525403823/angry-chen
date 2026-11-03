"""Manage a bounded, ephemeral P0 runner without printing credentials or starting jobs.
Prepare never starts Runner.Listener; the lead owns when its separate run.cmd starts.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import urllib.request
import zipfile

REPO = "FUG525403823/angry-chen"
API = "https://api.github.com"
ROOT = Path(__file__).resolve().parents[1]
RUNNER = ROOT / "build" / "scratch" / "p0-ci-runner"
STATE = RUNNER / "p0-registration.json"


def credential():
    env = dict(os.environ, GCM_INTERACTIVE="never")
    p = subprocess.run(["git", "-c", "credential.interactive=never", "credential", "fill"],
                       input="protocol=https\nhost=github.com\n\n", text=True,
                       capture_output=True, env=env, check=False)
    fields = dict(line.split("=", 1) for line in p.stdout.splitlines() if "=" in line)
    if p.returncode or not fields.get("password"):
        raise RuntimeError("Noninteractive GitHub credential unavailable (secret output suppressed)")
    return fields["password"]


def request(token, path, data=None, method=None):
    req = urllib.request.Request(API + path,
        headers={"Authorization": "Bearer " + token, "Accept": "application/vnd.github+json",
                 "X-GitHub-Api-Version": "2022-11-28", "User-Agent": "angry-chen-p0-ci"},
        data=None if data is None else json.dumps(data).encode(),
        method=method or ("GET" if data is None else "POST"))
    with urllib.request.urlopen(req, timeout=60) as response:
        body = response.read()
        return json.loads(body) if body else {}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action", choices=["status", "prepare", "remove"])
    args = parser.parse_args()
    token = credential()
    prefix = f"/repos/{REPO}/actions"
    if args.action == "status":
        runners = request(token, prefix + "/runners")
        permissions = request(token, prefix + "/permissions")
        print(json.dumps({"enabled": permissions.get("enabled"), "total_count": runners["total_count"],
            "runners": [{k: r[k] for k in ("id", "name", "status", "busy", "labels")} for r in runners["runners"]]}, indent=2))
        return
    if args.action == "remove":
        if not STATE.exists():
            raise RuntimeError("No owned runner registration record; refusing removal")
        state = json.loads(STATE.read_text())
        runners = request(token, prefix + "/runners")["runners"]
        target = [r for r in runners if r["name"] == state["name"]]
        if any(r["busy"] or r["status"] != "offline" for r in target):
            raise RuntimeError("Owned runner must be idle/offline; stop listener only after job settles")
        # Ephemeral runners deregister automatically after one job. DELETE also handles
        # interrupted local config without touching any runner not in our ownership record.
        for runner in target:
            request(token, prefix + "/runners/" + str(runner["id"]), method="DELETE")
        state["removed"] = True
        state["cleanupNote"] = "Remote registration removed or already absent; local credentials must remain private."
        STATE.write_text(json.dumps(state, indent=2), encoding="utf-8")
        # These are only runner-local credentials, never repository files or global Git credentials.
        for name in (".credentials", ".credentials_rsaparams", ".runner"):
            (RUNNER / name).unlink(missing_ok=True)
        print("OWNED RUNNER REMOVED " + state["name"])
        return
    if STATE.exists():
        previous = json.loads(STATE.read_text())
        if not previous.get("removed"):
            raise RuntimeError("Owned state still registered; inspect status/remove before preparing")
        STATE.replace(RUNNER / ("p0-registration-removed-" + previous["name"] + ".json"))
    if (RUNNER / ".runner").exists():
        raise RuntimeError("Runner directory already registered; inspect status, do not overwrite")
    downloads = request(token, prefix + "/runners/downloads")
    package = next(p for p in downloads if p["os"] == "win" and p["architecture"] == "x64")
    if not package.get("sha256_checksum"):
        raise RuntimeError("Official runner package lacks checksum; refusing unverified executable")
    RUNNER.mkdir(parents=True, exist_ok=True)
    archive = RUNNER / package["filename"]
    if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest().lower() != package["sha256_checksum"].lower():
        with urllib.request.urlopen(package["download_url"], timeout=120) as response:
            archive.write_bytes(response.read())
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest.lower() != package["sha256_checksum"].lower():
        raise RuntimeError("Official runner package checksum mismatch")
    with zipfile.ZipFile(archive) as zipped:
        for item in zipped.infolist():
            if not (RUNNER / item.filename).resolve().is_relative_to(RUNNER.resolve()):
                raise RuntimeError("Runner archive path escapes owned directory")
        zipped.extractall(RUNNER)
    import datetime
    name = "ac-p0-" + datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    registration = request(token, prefix + "/runners/registration-token", {})["token"]
    result = subprocess.run(["cmd.exe", "/d", "/c", "config.cmd", "--unattended", "--url", "https://github.com/" + REPO,
        "--token", registration, "--name", name, "--labels", "ac-unity-licensed", "--work", "_work", "--ephemeral", "--disableupdate"],
        cwd=RUNNER, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("Runner registration failed (configuration output suppressed)")
    with (RUNNER / ".env").open("a", encoding="utf-8") as envfile:
        envfile.write("\nAC_UNITY=D:/tools/unity/unity_install/2022.3.62t16/Editor/Tuanjie.exe\n")
    state = {"name": name, "directory": str(RUNNER), "package": package["filename"], "sha256": digest,
             "ephemeral": True, "started": False, "cleanup": "python tools/p0-ci-runner.py remove"}
    STATE.write_text(json.dumps(state, indent=2), encoding="utf-8")
    print(json.dumps(state, indent=2))
    print("REGISTERED OFFLINE: no listener started; no service installed; wait for lead before run.cmd")


if __name__ == "__main__":
    main()
