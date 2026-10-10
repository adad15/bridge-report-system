"""Ownership of one COM-created Office process; never infer it from a PID diff."""

from __future__ import annotations

import threading
import time
from pathlib import PureWindowsPath

from bridge_report_tools.reports.field_update_queue import FieldUpdateQueue

CLEANUP_SECONDS = 3.0


class OfficeProcess:
    """Keep the process handle alive so PID reuse cannot redirect termination."""

    def __init__(self, handle, pid: int) -> None:
        self.handle = handle
        self.pid = pid

    @classmethod
    def from_window(cls, hwnd: int, started_at: float) -> OfficeProcess:
        import win32api
        import win32process

        _, pid = win32process.GetWindowThreadProcessId(hwnd)
        # Query information, VM read (image name), terminate and synchronize.
        handle = win32api.OpenProcess(0x0400 | 0x0010 | 0x0001 | 0x00100000, False, pid)
        try:
            created = win32process.GetProcessTimes(handle)["CreationTime"].timestamp()
            image = PureWindowsPath(win32process.GetModuleFileNameEx(handle, 0)).name.lower()
            if image not in {"winword.exe", "wps.exe"} or created < started_at:
                raise RuntimeError("COM 窗口不属于本次新建的 Office 进程")
            return cls(handle, pid)
        except BaseException:
            handle.Close()
            raise

    def exited(self, timeout_seconds: float = 0) -> bool:
        import win32event

        return win32event.WaitForSingleObject(
            self.handle, int(timeout_seconds * 1000)
        ) == win32event.WAIT_OBJECT_0

    def terminate(self) -> bool:
        import win32api

        try:
            if not self.exited():
                win32api.TerminateProcess(self.handle, 1)
            return self.exited(CLEANUP_SECONDS)
        except Exception:
            return False

    def close(self) -> None:
        self.handle.Close()


class OfficeInvocation:
    """Synchronize late COM startup, timeout cleanup and worker completion."""

    def __init__(self, queue: FieldUpdateQueue) -> None:
        self.queue = queue
        # pywin32 exposes process creation time with millisecond precision.
        self.started_at = int(time.time() * 1000) / 1000
        self._lock = threading.Lock()
        self._process: OfficeProcess | None = None
        self._cancelled = False
        self._unverified_app = False

    def app_created(self) -> None:
        with self._lock:
            self._unverified_app = True

    def claim(self, process: OfficeProcess) -> None:
        with self._lock:
            self._process = process
            self._unverified_app = False
            if self._cancelled:
                process.terminate()
                raise RuntimeError("Office 启动完成时任务已超时")

    def block(self) -> None:
        self.queue.block(
            "上一次 Office 任务的进程归属或退出状态无法确认，已暂停字段更新。"
            "请核实并关闭残留的自动化实例后重启工具服务。"
        )

    def cancel(self) -> None:
        with self._lock:
            self._cancelled = True
            if self._process is None or not self._process.terminate():
                self.block()

    def finish(self) -> bool:
        clean = True
        with self._lock:
            if self._unverified_app:
                clean = False
                self.block()
            if self._process is not None:
                try:
                    if not self._process.exited(1.0) and not self._process.terminate():
                        clean = False
                        self.block()
                except Exception:
                    clean = False
                    self.block()
                finally:
                    self._process.close()
                    self._process = None
        return clean
