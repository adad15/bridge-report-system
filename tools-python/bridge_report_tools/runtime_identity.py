"""Frozen process identity; health probes never inspect changed source files."""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
import uuid


def source_digest(project: Path) -> str:
    paths = sorted((project / "bridge_report_tools").rglob("*.py"))
    paths += [project / "pyproject.toml", project / "uv.lock"]
    digest = hashlib.sha256()
    for path in sorted(paths):
        name = path.relative_to(project).as_posix().encode("utf-8")
        content = path.read_bytes()
        digest.update(len(name).to_bytes(8, "big"))
        digest.update(name)
        digest.update(len(content).to_bytes(8, "big"))
        digest.update(content)
    return digest.hexdigest()


def capture_identity(project: Path | None = None) -> dict[str, str | bool]:
    project = project or Path(__file__).resolve().parents[1]
    revision = "unavailable"
    dirty = False
    try:
        options = dict(cwd=project, capture_output=True, text=True, timeout=5)
        commit = subprocess.run(["git", "rev-parse", "HEAD"], **options)
        if commit.returncode == 0:
            revision = commit.stdout.strip()
            status = subprocess.run(
                ["git", "status", "--porcelain", "--untracked-files=normal"], **options
            )
            dirty = bool(status.stdout.strip())
    except (OSError, subprocess.TimeoutExpired):
        pass
    return {
        "source_digest": source_digest(project),
        "git_revision": revision,
        "worktree_dirty": dirty,
        "launch_id": os.environ.get("BRIDGE_REPORT_LAUNCH_ID", ""),
        "instance_id": str(uuid.uuid4()),
    }


if __name__ == "__main__":
    print(json.dumps(capture_identity()))
