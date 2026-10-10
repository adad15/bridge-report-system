"""No real Office processes: exercise timeout ownership and quarantine races."""

from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from threading import Event
from types import SimpleNamespace
import sys

import pytest

from bridge_report_tools.reports import field_updater as updater
from bridge_report_tools.reports.field_update_queue import FieldUpdateQueue, FieldUpdateQueueTimeout
from bridge_report_tools.reports.office_process import OfficeInvocation, OfficeProcess


class Process:
    def __init__(self, *, can_stop=True, stopped=None):
        self.can_stop = can_stop
        self.stopped = stopped or Event()
        self.kills = 0
        self.closed = False

    def exited(self, timeout_seconds=0):
        return self.stopped.is_set()

    def terminate(self):
        self.kills += 1
        if self.can_stop:
            self.stopped.set()
        return self.can_stop

    def close(self):
        self.closed = True


@pytest.fixture
def com(monkeypatch):
    monkeypatch.setitem(sys.modules, "pythoncom", SimpleNamespace(
        CoInitialize=lambda: None, CoUninitialize=lambda: None))
    monkeypatch.setattr(updater, "CLEANUP_SECONDS", 0.1)


def test_timeout_terminates_only_the_claimed_instance(com):
    queue = FieldUpdateQueue()
    owned, user_before, user_during = Process(), Process(), Process()
    worker = updater.microsoft_word_updater(timeout_seconds=0.03, queue=queue)

    def work(invocation):
        invocation.app_created()
        invocation.claim(owned)
        assert owned.stopped.wait(2)

    with pytest.raises(updater.FieldUpdateError, match="REPORT_FIELD_UPDATE_FAILED"):
        with queue.slot():
            worker._run_isolated(work, "test")
    assert owned.kills == 1
    assert owned.closed
    assert user_before.kills == user_during.kills == 0
    with queue.slot():
        pass


def test_late_com_start_is_quarantined_and_owned_process_is_cleaned(com):
    queue = FieldUpdateQueue()
    release, done = Event(), Event()
    process = Process()
    worker = updater.microsoft_word_updater(timeout_seconds=0.02, queue=queue)

    def work(invocation):
        release.wait(2)
        invocation.app_created()
        try:
            invocation.claim(process)
        finally:
            done.set()

    try:
        with pytest.raises(updater.FieldUpdateError):
            with queue.slot():
                worker._run_isolated(work, "test")
        with pytest.raises(FieldUpdateQueueTimeout) as error:
            with queue.slot():
                pytest.fail("must not launch a second Office instance")
        assert error.value.code == "REPORT_FIELD_UPDATE_RECOVERY_REQUIRED"
    finally:
        release.set()
    assert done.wait(2)
    assert process.kills == 1


def test_failed_termination_blocks_next_task(com):
    queue = FieldUpdateQueue()
    release = Event()
    process = Process(can_stop=False)
    worker = updater.microsoft_word_updater(timeout_seconds=0.02, queue=queue)

    def work(invocation):
        invocation.claim(process)
        release.wait(2)

    try:
        with pytest.raises(updater.FieldUpdateError):
            worker._run_isolated(work, "test")
        with pytest.raises(FieldUpdateQueueTimeout):
            with queue.slot():
                pass
    finally:
        release.set()


def test_normal_cleanup_failure_is_not_reported_as_success(com):
    queue = FieldUpdateQueue()
    worker = updater.microsoft_word_updater(queue=queue)

    def work(invocation):
        invocation.claim(Process(can_stop=False))
        return "success"

    with pytest.raises(RuntimeError, match="Office"):
        worker._run_isolated(work, "test")
    with pytest.raises(FieldUpdateQueueTimeout):
        with queue.slot():
            pass


def test_quarantine_wakes_already_waiting_tasks():
    queue = FieldUpdateQueue()
    waiting = Event()
    with ThreadPoolExecutor(max_workers=1) as pool:
        with queue.slot("first"):
            def wait():
                with queue.slot("second", lambda _: waiting.set()):
                    pytest.fail("blocked task entered Office")
            future = pool.submit(wait)
            assert waiting.wait(2)
            queue.block("unverified process")
            with pytest.raises(FieldUpdateQueueTimeout):
                future.result(timeout=2)
    assert queue.snapshot().depth == 0


@pytest.mark.parametrize("created,image,accepted", [
    (100.0, r"C:\Office\WINWORD.EXE", True),
    (100.0, r"C:\Office\wps.exe", True),
    (99.0, r"C:\Office\WINWORD.EXE", False),
    (100.0, r"C:\Other\app.exe", False),
])
def test_process_ownership_requires_window_identity_time_and_image(monkeypatch, created, image, accepted):
    handle = SimpleNamespace(closed=False)
    handle.Close = lambda: setattr(handle, "closed", True)
    monkeypatch.setitem(sys.modules, "win32api", SimpleNamespace(OpenProcess=lambda *args: handle))
    monkeypatch.setitem(sys.modules, "win32process", SimpleNamespace(
        GetWindowThreadProcessId=lambda hwnd: (7, 42) if hwnd == 123 else (0, 0),
        GetProcessTimes=lambda h: {"CreationTime": datetime.fromtimestamp(created, timezone.utc)},
        GetModuleFileNameEx=lambda h, _: image,
    ))
    if accepted:
        process = OfficeProcess.from_window(123, 100.0)
        assert process.pid == 42 and process.handle is handle
        process.close()
    else:
        with pytest.raises(RuntimeError):
            OfficeProcess.from_window(123, 100.0)
    assert handle.closed


def test_unverified_com_instance_is_never_quit_or_reconfigured(monkeypatch):
    app = SimpleNamespace(Documents=SimpleNamespace(Count=1))
    client = SimpleNamespace(DispatchEx=lambda _: app)
    monkeypatch.setitem(sys.modules, "win32com", SimpleNamespace(client=client))
    monkeypatch.setitem(sys.modules, "win32com.client", client)
    queue = FieldUpdateQueue()
    invocation = OfficeInvocation(queue)
    with pytest.raises(RuntimeError, match="已有文档"):
        updater.microsoft_word_updater(queue=queue)._new_app(invocation)
    assert not hasattr(app, "Visible")
    assert not invocation.finish()
    with pytest.raises(FieldUpdateQueueTimeout):
        with queue.slot():
            pass


def test_termination_uses_retained_handle_not_reopened_pid(monkeypatch):
    handle = object()
    calls = []
    monkeypatch.setitem(sys.modules, "win32api", SimpleNamespace(
        TerminateProcess=lambda h, code: calls.append(h),
        OpenProcess=lambda *args: pytest.fail("must not reopen a possibly reused PID"),
    ))
    monkeypatch.setitem(sys.modules, "win32event", SimpleNamespace(
        WAIT_OBJECT_0=0,
        WaitForSingleObject=lambda h, timeout: 0 if calls else 258,
    ))
    assert OfficeProcess(handle, 42).terminate()
    assert calls == [handle]


def test_window_marker_stays_open_until_application_quits(monkeypatch):
    marker = SimpleNamespace(closed=False, Windows=lambda _: SimpleNamespace(Hwnd=123))
    marker.Close = lambda _: setattr(marker, "closed", True)
    app = SimpleNamespace(
        Documents=SimpleNamespace(Count=0, Add=lambda **kwargs: marker),
        Options=SimpleNamespace(),
    )
    client = SimpleNamespace(DispatchEx=lambda _: app)
    monkeypatch.setitem(sys.modules, "win32com", SimpleNamespace(client=client))
    monkeypatch.setitem(sys.modules, "win32com.client", client)
    process = Process()
    monkeypatch.setattr(OfficeProcess, "from_window", lambda *_: process)
    invocation = OfficeInvocation(FieldUpdateQueue())
    assert updater.microsoft_word_updater()._new_app(invocation) is app
    assert not marker.closed
    assert app.Visible is False
    assert invocation.finish()
    assert process.closed


def test_update_failure_exposes_unconfirmed_writer_to_backend(tmp_path, monkeypatch):
    queue = FieldUpdateQueue()
    worker = updater.microsoft_word_updater(queue=queue)
    source = tmp_path / "input.docx"
    source.write_bytes(b"placeholder")
    def fail(*args, **kwargs):
        queue.block("process did not exit")
        raise RuntimeError("cleanup failed")
    monkeypatch.setattr(worker, "_run_isolated", fail)
    with pytest.raises(updater.FieldUpdateError) as error:
        worker.update_fields(source, tmp_path / "out.docx")
    assert error.value.writer_uncertain
    assert error.value.stage == "update"


def test_ordinary_finished_failure_does_not_block_artifact_cleanup(tmp_path, monkeypatch):
    worker = updater.microsoft_word_updater(queue=FieldUpdateQueue())
    source = tmp_path / "input.docx"
    source.write_bytes(b"placeholder")
    def fail(*args, **kwargs):
        raise RuntimeError("invalid document")
    monkeypatch.setattr(worker, "_run_isolated", fail)
    with pytest.raises(updater.FieldUpdateError) as error:
        worker.update_fields(source, tmp_path / "out.docx")
    assert not error.value.writer_uncertain
