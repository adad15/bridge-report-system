from pathlib import Path
import uuid
from bridge_report_tools.runtime_identity import capture_identity, source_digest
from bridge_report_tools.main import health, RUNTIME_IDENTITY


def project(tmp_path: Path) -> Path:
    (tmp_path / "bridge_report_tools").mkdir()
    (tmp_path / "bridge_report_tools" / "entry.py").write_text("VALUE = 1", encoding="utf-8")
    (tmp_path / "pyproject.toml").write_text("[project]", encoding="utf-8")
    (tmp_path / "uv.lock").write_text("version = 1", encoding="utf-8")
    return tmp_path


def test_source_edit_changes_digest_without_commit(tmp_path):
    root = project(tmp_path)
    old = source_digest(root)
    (root / "bridge_report_tools" / "entry.py").write_text("VALUE = 2", encoding="utf-8")
    assert source_digest(root) != old


def test_lockfile_change_changes_digest(tmp_path):
    root = project(tmp_path)
    old = source_digest(root)
    (root / "uv.lock").write_text("version = 2", encoding="utf-8")
    assert source_digest(root) != old


def test_identity_is_frozen_but_new_instance_changes(tmp_path, monkeypatch):
    root = project(tmp_path)
    monkeypatch.setenv("BRIDGE_REPORT_LAUNCH_ID", "launch-a")
    first = capture_identity(root)
    (root / "bridge_report_tools" / "entry.py").write_text("VALUE = 3", encoding="utf-8")
    monkeypatch.setenv("BRIDGE_REPORT_LAUNCH_ID", "launch-b")
    second = capture_identity(root)
    assert first["source_digest"] != second["source_digest"]
    assert first["launch_id"] == "launch-a"
    assert second["launch_id"] == "launch-b"
    assert first["instance_id"] != second["instance_id"]
    uuid.UUID(first["instance_id"])


def test_health_does_not_recapture_environment(monkeypatch):
    before = health().model_dump()
    monkeypatch.setenv("BRIDGE_REPORT_LAUNCH_ID", "changed-environment")
    assert health().model_dump() == before
    assert before["instance_id"] == RUNTIME_IDENTITY["instance_id"]
