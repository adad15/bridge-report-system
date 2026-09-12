"""字段更新队列的行为测试。不启动 Word，只验证排队语义。"""

from __future__ import annotations

import threading
import time

import pytest

from bridge_report_tools.reports.field_update_queue import (
    FieldUpdateQueue,
    FieldUpdateQueueTimeout,
)


def wait_for(predicate, timeout: float = 5.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        time.sleep(0.005)
    raise AssertionError("等待条件超时")


def test_single_caller_runs_immediately_without_waiting() -> None:
    queue = FieldUpdateQueue()
    positions: list[int] = []

    with queue.slot("word", positions.append):
        assert queue.snapshot().running == "word"

    assert positions == []
    assert queue.snapshot() .depth == 0


def test_second_caller_waits_until_the_first_releases() -> None:
    queue = FieldUpdateQueue()
    release = threading.Event()
    order: list[str] = []

    def hold() -> None:
        with queue.slot("first"):
            order.append("first-in")
            release.wait(5)
            order.append("first-out")

    def follow() -> None:
        with queue.slot("second"):
            order.append("second-in")

    holder = threading.Thread(target=hold)
    holder.start()
    wait_for(lambda: order == ["first-in"])

    follower = threading.Thread(target=follow)
    follower.start()
    wait_for(lambda: queue.snapshot().waiting == ("second",))

    # 第一个还没放手，第二个绝不能进去。
    time.sleep(0.1)
    assert order == ["first-in"]

    release.set()
    holder.join(5)
    follower.join(5)

    assert order == ["first-in", "first-out", "second-in"]


def test_waiters_are_served_first_come_first_served() -> None:
    """用票号而不是 Lock 就是为了这个：排在后面的人不会被后到的请求反复插队。"""
    queue = FieldUpdateQueue()
    release = threading.Event()
    served: list[str] = []

    def hold() -> None:
        with queue.slot("holder"):
            release.wait(5)

    def follow(name: str) -> None:
        with queue.slot(name):
            served.append(name)

    holder = threading.Thread(target=hold)
    holder.start()
    wait_for(lambda: queue.snapshot().running == "holder")

    followers = []
    for name in ("a", "b", "c"):
        thread = threading.Thread(target=follow, args=(name,))
        thread.start()
        followers.append(thread)
        # 逐个确认已入队，才能保证入队顺序确定。
        wait_for(lambda n=name: n in queue.snapshot().waiting)

    assert queue.snapshot().waiting == ("a", "b", "c")

    release.set()
    holder.join(5)
    for thread in followers:
        thread.join(5)

    assert served == ["a", "b", "c"]


def test_wait_callback_reports_how_many_are_ahead() -> None:
    queue = FieldUpdateQueue()
    release = threading.Event()
    reported: list[int] = []

    def hold() -> None:
        with queue.slot("holder"):
            release.wait(5)

    def follow(sink: list[int]) -> None:
        with queue.slot("follower", sink.append):
            pass

    holder = threading.Thread(target=hold)
    holder.start()
    wait_for(lambda: queue.snapshot().running == "holder")

    first_sink: list[int] = []
    second_sink: list[int] = []
    a = threading.Thread(target=follow, args=(first_sink,))
    a.start()
    wait_for(lambda: len(queue.snapshot().waiting) == 1)
    b = threading.Thread(target=follow, args=(second_sink,))
    b.start()
    wait_for(lambda: len(queue.snapshot().waiting) == 2)

    release.set()
    holder.join(5)
    a.join(5)
    b.join(5)

    # 前面各有 1 个和 2 个。
    assert first_sink == [1]
    assert second_sink == [2]
    reported.extend(first_sink + second_sink)
    assert reported == [1, 2]


def test_exception_inside_the_slot_still_releases_it() -> None:
    queue = FieldUpdateQueue()

    with pytest.raises(RuntimeError):
        with queue.slot("boom"):
            raise RuntimeError("字段更新炸了")

    assert queue.snapshot().depth == 0
    # 位置必须能立刻再被占用，否则一次失败会把队列永久堵死。
    with queue.slot("next"):
        assert queue.snapshot().running == "next"


def test_waiting_too_long_raises_and_does_not_block_the_next_in_line() -> None:
    queue = FieldUpdateQueue(wait_timeout_seconds=0.05)
    release = threading.Event()
    errors: list[FieldUpdateQueueTimeout] = []
    served: list[str] = []

    def hold() -> None:
        with queue.slot("holder"):
            release.wait(5)

    def give_up() -> None:
        try:
            with queue.slot("impatient"):
                served.append("impatient")
        except FieldUpdateQueueTimeout as exc:
            errors.append(exc)

    holder = threading.Thread(target=hold)
    holder.start()
    wait_for(lambda: queue.snapshot().running == "holder")

    impatient = threading.Thread(target=give_up)
    impatient.start()
    impatient.join(5)

    assert len(errors) == 1
    assert errors[0].code == "REPORT_FIELD_UPDATE_QUEUE_TIMEOUT"
    assert served == []

    # 放弃者的票号必须被跳过，否则它后面的人永远等不到。
    patient_queue_entry = threading.Thread(target=lambda: served.append(_take(queue)))
    patient_queue_entry.start()
    wait_for(lambda: queue.snapshot().waiting == ("patient",))
    release.set()
    holder.join(5)
    patient_queue_entry.join(5)

    assert served == ["patient"]


def _take(queue: FieldUpdateQueue) -> str:
    with queue.slot("patient"):
        return "patient"


def test_snapshot_reports_running_and_waiting_labels() -> None:
    queue = FieldUpdateQueue()
    release = threading.Event()

    def hold() -> None:
        with queue.slot("MicrosoftWordUpdater"):
            release.wait(5)

    def follow() -> None:
        with queue.slot("WpsWriterUpdater"):
            pass

    holder = threading.Thread(target=hold)
    holder.start()
    wait_for(lambda: queue.snapshot().running == "MicrosoftWordUpdater")

    follower = threading.Thread(target=follow)
    follower.start()
    wait_for(lambda: queue.snapshot().waiting == ("WpsWriterUpdater",))

    snapshot = queue.snapshot()
    assert snapshot.running == "MicrosoftWordUpdater"
    assert snapshot.waiting == ("WpsWriterUpdater",)
    assert snapshot.depth == 2

    release.set()
    holder.join(5)
    follower.join(5)
    assert queue.snapshot().depth == 0
