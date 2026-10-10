"""字段更新的全局 FIFO 队列，并发上限固定为 1（设计 §17.3）。

Office 共享临时文件和配置，多份大型文档并行排版容易挂起并消耗大量内存。
若上一次任务无法确认进程归属或退出状态，队列会阻断后续任务，直到核实并重启服务。

排队的只是最后进 Word 那一步。Docx Builder 装配文档的阶段可以并行（设计 §17.3）。

用票号而不是 threading.Lock：Lock 不保证先来先服务，报告生成排在后面的人可能被
后到的请求反复插队，等待时长无法预期，界面也没法说"你前面还有几个"。
"""

from __future__ import annotations

import threading
import time
from contextlib import contextmanager
from dataclasses import dataclass, field
from typing import Callable, Iterator


#: 排队等待的上限。真实报告的字段更新可能要几分钟，等待上限必须比它宽得多，
#: 但也不能无限——否则一个卡死的更新会把后面所有任务永久挂住。
DEFAULT_QUEUE_WAIT_SECONDS = 1800.0

#: 传给调用方的通知：你前面还有几个人。position 为 0 表示立即开始。
WaitCallback = Callable[[int], None]


@dataclass
class FieldUpdateQueueTimeout(Exception):
    """排队超时。任务没有进入 Word，输出文件也不曾创建。"""

    code: str
    message: str
    waited_seconds: float

    def __str__(self) -> str:
        return f"{self.code}: {self.message}"


@dataclass(frozen=True)
class QueueSnapshot:
    """队列当前状态，供任务页面显示"等待字段更新"而不是让用户看着转圈。"""

    running: str | None
    waiting: tuple[str, ...] = ()

    @property
    def depth(self) -> int:
        return (1 if self.running is not None else 0) + len(self.waiting)


@dataclass
class _Ticket:
    number: int
    label: str


class FieldUpdateQueue:
    """票号队列：先来先服务，同时只放行一个。"""

    def __init__(self, wait_timeout_seconds: float = DEFAULT_QUEUE_WAIT_SECONDS) -> None:
        self._condition = threading.Condition()
        self._wait_timeout_seconds = wait_timeout_seconds
        self._next_number = 0
        self._now_serving = 0
        self._tickets: dict[int, _Ticket] = {}
        #: 等不下去自行离开的票号。轮到它们时要跳过，否则后面的人永远等不到。
        self._abandoned: set[int] = set()
        self._blocked_reason: str | None = None

    # ---- 状态 --------------------------------------------------------------

    def block(self, reason: str) -> None:
        with self._condition:
            self._blocked_reason = reason
            self._condition.notify_all()

    def _check_blocked_locked(self) -> None:
        if self._blocked_reason:
            raise FieldUpdateQueueTimeout(
                code="REPORT_FIELD_UPDATE_RECOVERY_REQUIRED",
                message=self._blocked_reason,
                waited_seconds=0.0,
            )

    @property
    def blocked(self) -> bool:
        with self._condition:
            return self._blocked_reason is not None

    def snapshot(self) -> QueueSnapshot:
        with self._condition:
            running = self._tickets.get(self._now_serving)
            waiting = tuple(
                ticket.label
                for number, ticket in sorted(self._tickets.items())
                if number > self._now_serving
            )
            return QueueSnapshot(
                running=running.label if running is not None else None,
                waiting=waiting,
            )

    # ---- 进出 --------------------------------------------------------------

    def _skip_abandoned_locked(self) -> None:
        while self._now_serving in self._abandoned:
            self._abandoned.discard(self._now_serving)
            self._now_serving += 1

    @contextmanager
    def slot(self, label: str = "", on_wait: WaitCallback | None = None) -> Iterator[None]:
        """占用唯一的执行位。前面有人时阻塞，并通过 on_wait 告知排在第几位。"""
        started = time.monotonic()
        with self._condition:
            self._check_blocked_locked()
            number = self._next_number
            self._next_number += 1
            self._tickets[number] = _Ticket(number, label)
            ahead = number - self._now_serving

            if ahead > 0 and on_wait is not None:
                # 在锁内回调：调用方只是记一条状态，不该在这里做慢活。
                on_wait(ahead)

            if ahead > 0:
                acquired = self._condition.wait_for(
                    lambda: self._now_serving == number or self._blocked_reason is not None,
                    timeout=self._wait_timeout_seconds,
                )
                if self._blocked_reason:
                    self._tickets.pop(number, None)
                    self._abandoned.add(number)
                    self._check_blocked_locked()
                if not acquired:
                    self._tickets.pop(number, None)
                    self._abandoned.add(number)
                    # 自己还没轮到，now_serving 不动；轮到时由 _skip_abandoned_locked 跳过。
                    raise FieldUpdateQueueTimeout(
                        code="REPORT_FIELD_UPDATE_QUEUE_TIMEOUT",
                        message=(
                            f"等待字段更新排队超过 {self._wait_timeout_seconds:.0f} 秒，"
                            "前面的任务仍未完成。"
                        ),
                        waited_seconds=time.monotonic() - started,
                    )

        try:
            yield
        finally:
            with self._condition:
                self._tickets.pop(number, None)
                self._now_serving = number + 1
                self._skip_abandoned_locked()
                self._condition.notify_all()


#: 进程内唯一的队列。字段更新主机是单进程的本地服务，进程内串行即等于主机内串行。
FIELD_UPDATE_QUEUE = FieldUpdateQueue()
