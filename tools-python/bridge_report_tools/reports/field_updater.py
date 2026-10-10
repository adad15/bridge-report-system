"""Word / WPS 域更新器（设计 §19）。

只有真实排版引擎能算出目录页码和总页数，所以这一步必须交给 Word 或 WPS。业务内容一律
不经过它们：进来的中间文档已经由 Docx Builder 写定，这里只刷新 TOC / PAGE / NUMPAGES。

部署边界：第一版是本地 Windows 桌面应用，更新器必须运行在已登录的交互式会话中。
不要把它放进 Windows Service 或无桌面会话——Office 自动化在那种环境下不受支持。

两条安全红线：
1. 用 DispatchEx 起**新**实例，绝不 attach 到用户正开着的 Word/WPS；收尾也只关自己起的。
2. 强制关闭宏，并禁止打开时更新外部链接。
"""

from __future__ import annotations

import shutil
import time
from concurrent.futures import Future, TimeoutError as FutureTimeoutError
from threading import Thread
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Protocol

from bridge_report_tools.reports.errors import ReportTemplateError
from bridge_report_tools.reports.office_process import CLEANUP_SECONDS, OfficeInvocation, OfficeProcess
from bridge_report_tools.reports.field_update_queue import (
    FIELD_UPDATE_QUEUE,
    FieldUpdateQueue,
    FieldUpdateQueueTimeout,
    WaitCallback,
)


# Word 对象模型常量。WPS 的 Writer 实现同一套模型，取值一致。
WD_ALERTS_NONE = 0
MSO_AUTOMATION_SECURITY_FORCE_DISABLE = 3
WD_FORMAT_DOCUMENT_DEFAULT = 16
WD_STATISTIC_PAGES = 2
WD_DO_NOT_SAVE_CHANGES = 0
#: wdHeaderFooterPrimary / FirstPage / EvenPages。
HEADER_FOOTER_KINDS = (1, 2, 3)

DEFAULT_TIMEOUT_SECONDS = 300.0

UPDATER_MICROSOFT_WORD = "MicrosoftWordUpdater"
UPDATER_WPS_WRITER = "WpsWriterUpdater"


@dataclass
class FieldUpdateError(Exception):
    """域更新失败。stage 指出卡在哪一步，便于运行排障（设计 §19）。"""

    code: str
    message: str
    stage: str
    updater: str
    writer_uncertain: bool = False

    def __str__(self) -> str:
        return f"{self.code}[{self.updater}/{self.stage}]: {self.message}"


@dataclass(frozen=True)
class ProbeResult:
    updater: str
    available: bool
    version: str | None = None
    detail: str | None = None


@dataclass(frozen=True)
class UpdateOutcome:
    updater: str
    output_path: Path
    #: 真正在 Word/WPS 里的时长，不含排队。
    elapsed_seconds: float
    page_count: int | None
    #: 排队等待时长。§25.3 的规模验收要分别记录这两项。
    queued_seconds: float = 0.0


class OfficeFieldUpdater(Protocol):
    name: str

    def probe(self) -> ProbeResult: ...

    def update_fields(
        self,
        input_docx: Path,
        output_docx: Path,
        on_wait: WaitCallback | None = None,
    ) -> UpdateOutcome: ...


def _field_stories(document):
    """要刷域的全部范围：各顶层故事区，加上每一节的页眉页脚。

    **不能用 NextStoryRange 把链走下去。** 本机的 WPS（对外自称 Microsoft Word 12.0）
    在这条链上不收敛：同一个 39 个字符的页眉无限重复，域更新永远跑不完。实测 900 秒
    超时抛出时，它还在原地转第九万次。按节取页眉页脚是有界的，覆盖面也正好是本设计
    需要的那些——目录、页码和总页数只出现在正文和页眉页脚里，别处的域模板契约本来
    就不允许（§7.6）。

    第一节的主页眉会被刷两遍（顶层故事区里有它，按节取时又有一次），无所谓。
    """
    for story in document.StoryRanges:
        yield story
    for index in range(1, document.Sections.Count + 1):
        section = document.Sections(index)
        for collection in (section.Headers, section.Footers):
            for kind in HEADER_FOOTER_KINDS:
                try:
                    yield collection(kind).Range
                except Exception:
                    continue


class ComWordUpdater:
    """基于 Word 对象模型的更新器。Word 与 WPS Writer 共用这套实现，只换 ProgID。"""

    def __init__(
        self,
        name: str,
        prog_id: str,
        timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
        queue: FieldUpdateQueue | None = None,
    ) -> None:
        self.name = name
        self.prog_id = prog_id
        self.timeout_seconds = timeout_seconds
        # 探测和实际更新共享 FIFO，无法确认清理结果时阻断后续任务。
        self._queue = queue if queue is not None else FIELD_UPDATE_QUEUE

    # ---- COM 生命周期 ------------------------------------------------------

    def _run_isolated(self, work: Callable[[OfficeInvocation], object], stage: str) -> object:
        """Run COM in its own apartment and clean only the verified process handle."""
        invocation = OfficeInvocation(self._queue)
        future: Future = Future()

        def wrapped() -> None:
            try:
                import pythoncom

                pythoncom.CoInitialize()
                try:
                    result = work(invocation)
                finally:
                    try:
                        pythoncom.CoUninitialize()
                    finally:
                        if not invocation.finish():
                            raise RuntimeError("Office 实例未能安全清理，后续字段更新已暂停")
                future.set_result(result)
            except BaseException as exc:
                future.set_exception(exc)

        # A stuck COM call must not keep the Python service alive during shutdown.
        Thread(target=wrapped, name=f"office-{stage}", daemon=True).start()
        try:
            return future.result(timeout=self.timeout_seconds)
        except FutureTimeoutError:
            invocation.cancel()
            try:
                future.result(timeout=CLEANUP_SECONDS)
            except FutureTimeoutError:
                invocation.block()
            except BaseException:
                pass
            raise FieldUpdateError(
                code="REPORT_FIELD_UPDATE_FAILED",
                message=(
                    f"{self.name} 在 {self.timeout_seconds:.0f} 秒内没有完成域更新。"
                    "仅清理已确认归属的自动化进程；无法确认清理结果时暂停后续任务。"
                ),
                stage="timeout",
                updater=self.name,
                writer_uncertain=True,
            ) from None

    def _new_app(self, invocation: OfficeInvocation):
        import win32com.client

        # DispatchEx 必须用：Dispatch 会 attach 到用户已经开着的实例，之后的 Quit
        # 会连人家没存的文档一起关掉。
        app = win32com.client.DispatchEx(self.prog_id)
        invocation.app_created()
        # DispatchEx may still be redirected by an Office-compatible COM server.
        # Do not change settings or Quit an instance which already has documents.
        if app.Documents.Count:
            raise RuntimeError("COM 返回了已有文档的 Office 实例，拒绝接管")
        marker = None
        try:
            try:
                hwnd = int(app.Hwnd)
            except Exception:
                # Word exposes Hwnd on Window, not Application.
                marker = app.Documents.Add(Visible=False)
                hwnd = int(marker.Windows(1).Hwnd)
            invocation.claim(OfficeProcess.from_window(hwnd, invocation.started_at))
        except BaseException:
            if marker is not None:
                # Only this temporary, empty document belongs to this call.
                try:
                    marker.Close(WD_DO_NOT_SAVE_CHANGES)
                except Exception:
                    pass
            raise
        # Keep the marker alive until Quit: some WPS versions exit when their last
        # document closes, invalidating the Application proxy before work starts.
        app.Visible = False
        app.DisplayAlerts = WD_ALERTS_NONE
        try:
            app.AutomationSecurity = MSO_AUTOMATION_SECURITY_FORCE_DISABLE
        except Exception:
            # WPS 的部分版本没有这个属性；宏在模板上传时已被拒（设计 §23.1），
            # 这里是纵深防御而不是唯一防线。
            pass
        try:
            app.Options.UpdateLinksAtOpen = False
        except Exception:
            pass
        return app

    @staticmethod
    def _quit(app) -> None:
        try:
            app.Quit(WD_DO_NOT_SAVE_CHANGES)
        except Exception:
            pass

    # ---- 对外接口 ----------------------------------------------------------

    def probe(self, on_wait: WaitCallback | None = None) -> ProbeResult:
        """探测更新器是否可用。同样要排队。

        探测也会创建并核实 Office 实例，和更新共用队列及进程清理规则。

        代价是：另一份报告正在更新域时，生成前检查（设计 §16 第 9 条）会等在队列里。
        这是可接受的——那时本来也轮不到你用 Word。
        """

        def work(invocation: OfficeInvocation) -> ProbeResult:
            try:
                app = self._new_app(invocation)
            except Exception as exc:
                return ProbeResult(self.name, False, detail=f"无法创建 COM 实例：{exc}")
            try:
                try:
                    version = str(app.Version)
                except Exception:
                    version = None
                return ProbeResult(self.name, True, version=version)
            finally:
                self._quit(app)

        try:
            with self._queue.slot(f"{self.name}（探测）", on_wait):
                return self._run_isolated(work, stage="probe")  # type: ignore[return-value]
        except FieldUpdateQueueTimeout as exc:
            return ProbeResult(self.name, False, detail=exc.message)
        except FieldUpdateError as exc:
            return ProbeResult(self.name, False, detail=exc.message)
        except Exception as exc:  # pragma: no cover - 环境相关
            return ProbeResult(self.name, False, detail=str(exc))

    def update_fields(
        self,
        input_docx: Path,
        output_docx: Path,
        on_wait: WaitCallback | None = None,
    ) -> UpdateOutcome:
        # 输入不存在要当场报错，不能先去排半小时队再发现。
        if not input_docx.is_file():
            raise FieldUpdateError(
                code="REPORT_FIELD_UPDATE_FAILED",
                message=f"待更新文档不存在：{input_docx}",
                stage="open",
                updater=self.name,
            )

        queued_at = time.monotonic()
        with self._queue.slot(self.name, on_wait):
            queued_seconds = time.monotonic() - queued_at
            return self._update_now(input_docx, output_docx, queued_seconds)

    def _update_now(
        self, input_docx: Path, output_docx: Path, queued_seconds: float
    ) -> UpdateOutcome:
        # 先复制再打开：Word 绝不接触 Builder 的产物，失败时删掉副本即可，
        # 也不会在输入文件旁边留下 ~$ owner file。
        output_docx.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(input_docx, output_docx)
        target = str(output_docx.resolve())
        started = time.monotonic()

        def work(invocation: OfficeInvocation) -> int | None:
            app = self._new_app(invocation)
            document = None
            try:
                document = app.Documents.Open(
                    FileName=target,
                    ConfirmConversions=False,
                    ReadOnly=False,
                    AddToRecentFiles=False,
                    Visible=False,
                )
                # 先刷正文和页眉页脚里的域，再重排版，最后单独更新目录——
                # 目录页码依赖最终排版结果，顺序反了会写进过期页码。
                for story in _field_stories(document):
                    try:
                        story.Fields.Update()
                    except Exception:
                        pass
                document.Repaginate()
                for index in range(1, document.TablesOfContents.Count + 1):
                    document.TablesOfContents(index).Update()
                document.Repaginate()

                try:
                    pages = int(document.ComputeStatistics(WD_STATISTIC_PAGES))
                except Exception:
                    pages = None

                document.SaveAs2(target, FileFormat=WD_FORMAT_DOCUMENT_DEFAULT)
                document.Close(WD_DO_NOT_SAVE_CHANGES)
                document = None
                return pages
            finally:
                if document is not None:
                    try:
                        document.Close(WD_DO_NOT_SAVE_CHANGES)
                    except Exception:
                        pass
                self._quit(app)

        try:
            pages = self._run_isolated(work, stage="update")
        except FieldUpdateError:
            try:
                output_docx.unlink(missing_ok=True)
            except OSError:
                pass  # A still-locked output must not mask the original failure.
            raise
        except Exception as exc:
            try:
                output_docx.unlink(missing_ok=True)
            except OSError:
                pass  # A still-locked output must not mask the original failure.
            raise FieldUpdateError(
                code="REPORT_FIELD_UPDATE_FAILED",
                message=f"{self.name} 更新域失败：{exc}",
                stage="update",
                updater=self.name,
                writer_uncertain=self._queue.blocked,
            ) from exc

        return UpdateOutcome(
            updater=self.name,
            output_path=output_docx,
            elapsed_seconds=time.monotonic() - started,
            page_count=pages,  # type: ignore[arg-type]
            queued_seconds=queued_seconds,
        )


def microsoft_word_updater(
    timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
    queue: FieldUpdateQueue | None = None,
) -> ComWordUpdater:
    return ComWordUpdater(
        name=UPDATER_MICROSOFT_WORD,
        prog_id="Word.Application",
        timeout_seconds=timeout_seconds,
        queue=queue,
    )


def wps_writer_updater(
    timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
    queue: FieldUpdateQueue | None = None,
) -> ComWordUpdater:
    # WPS 的 Writer 注册为 KWPS.Application，实现的是 Word 对象模型。
    return ComWordUpdater(
        name=UPDATER_WPS_WRITER,
        prog_id="KWPS.Application",
        timeout_seconds=timeout_seconds,
        queue=queue,
    )


def available_updaters(
    timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS,
    queue: FieldUpdateQueue | None = None,
) -> list[ComWordUpdater]:
    """按设计 §19 的优先级返回更新器：先 Word，Word 不可用再退 WPS。

    两个更新器共用同一个全局队列——瓶颈是这台机器，不是某一个 Office 实现。
    """
    return [
        microsoft_word_updater(timeout_seconds, queue),
        wps_writer_updater(timeout_seconds, queue),
    ]


def probe_all(timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS) -> list[ProbeResult]:
    return [updater.probe() for updater in available_updaters(timeout_seconds)]


def select_updater(timeout_seconds: float = DEFAULT_TIMEOUT_SECONDS) -> ComWordUpdater:
    """挑一个可用的更新器；都不可用时报 REPORT_FIELD_UPDATER_UNAVAILABLE。"""
    details: list[str] = []
    for updater in available_updaters(timeout_seconds):
        result = updater.probe()
        if result.available:
            return updater
        details.append(f"{result.updater}: {result.detail or '不可用'}")
    raise ReportTemplateError(
        "REPORT_FIELD_UPDATER_UNAVAILABLE",
        "本机没有可用的 Word 或 WPS 域更新器。" + "；".join(details),
    )
