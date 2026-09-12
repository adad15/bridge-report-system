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
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, TimeoutError as FutureTimeoutError
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Iterable, Protocol

from bridge_report_tools.reports.errors import ReportTemplateError
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

CREATE_NO_WINDOW = 0x08000000
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


#: 可能被 COM 起来的办公软件映像名。
#:
#: 两个都要看，不能只看自己那一个：WPS 会把 `Word.Application` 这个 ProgID 抢注到
#: 自己名下，于是"Word 更新器"实际起的是 wps.exe。只按 WINWORD.EXE 找 PID 会一个
#: 都找不到，超时清理什么也杀不掉，更新线程就永远挂在那儿（本机实测：900 秒超时
#: 抛了异常，进程却卡了半小时，直到手工杀掉那个 wps.exe）。
#:
#: 误伤用户自己开着的实例由 PID 差集拦住，与映像名无关——只有本次启动之后才出现的
#: PID 才允许被杀。
OFFICE_IMAGE_NAMES: tuple[str, ...] = ("WINWORD.EXE", "wps.exe")


def _running_pids(image_names: Iterable[str] = OFFICE_IMAGE_NAMES) -> set[int]:
    """当前这些映像名下的进程集合。

    用来把"我们起的实例"和"用户自己开着的"区分开：只有差集里的 PID 才允许被强杀。
    """
    pids: set[int] = set()
    for image_name in image_names:
        try:
            completed = subprocess.run(
                ["tasklist", "/FI", f"IMAGENAME eq {image_name}", "/NH", "/FO", "CSV"],
                capture_output=True,
                text=True,
                timeout=15,
                creationflags=CREATE_NO_WINDOW,
            )
        except (OSError, subprocess.SubprocessError):
            continue

        for line in completed.stdout.splitlines():
            fields = [part.strip('"') for part in line.strip().split('","')]
            if len(fields) < 2:
                continue
            try:
                pids.add(int(fields[1]))
            except ValueError:
                continue
    return pids


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


def _kill(pid: int) -> None:
    try:
        subprocess.run(
            ["taskkill", "/PID", str(pid), "/F", "/T"],
            capture_output=True,
            timeout=20,
            creationflags=CREATE_NO_WINDOW,
        )
    except (OSError, subprocess.SubprocessError):
        pass


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
        # 队列在这一层而不是调用方：超时清理靠进程差集认领自己起的 Word，
        # 并发时会杀错人（见 field_update_queue 的模块说明）。防护必须和被防护的
        # 代码待在一起，否则谁直接调 update_fields 谁就把 bug 带回来了。
        self._queue = queue if queue is not None else FIELD_UPDATE_QUEUE

    # ---- COM 生命周期 ------------------------------------------------------

    def _run_isolated(self, work: Callable[[], object], stage: str) -> object:
        """在独立线程里跑 COM 工作，超时后只强杀本次新起的进程。

        COM 调用是同步阻塞的，没法从外面打断；唯一可靠的解除办法是干掉那个进程，
        因此必须先记下它的 PID，否则超时清理会误伤用户的 Word。
        """
        before = _running_pids()
        owned: set[int] = set()

        def wrapped() -> object:
            import pythoncom

            pythoncom.CoInitialize()
            try:
                return work()
            finally:
                pythoncom.CoUninitialize()

        pool = ThreadPoolExecutor(max_workers=1)
        try:
            future = pool.submit(wrapped)
            try:
                return future.result(timeout=self.timeout_seconds)
            except FutureTimeoutError:
                owned = _running_pids() - before
                for pid in owned:
                    _kill(pid)
                raise FieldUpdateError(
                    code="REPORT_FIELD_UPDATE_FAILED",
                    message=(
                        f"{self.name} 在 {self.timeout_seconds:.0f} 秒内没有完成域更新，"
                        f"已强制结束本次启动的 {len(owned)} 个进程。"
                    ),
                    stage="timeout",
                    updater=self.name,
                ) from None
        finally:
            # 不能用 with：它退出时会 shutdown(wait=True)，等那个已经超时的 COM 调用
            # 返回。杀进程通常能让它返回，但没杀到时（比如 PID 差集是空的）就永远等
            # 下去——超时抛了异常，调用方却收不到，整个进程挂死。宁可让线程漏着。
            pool.shutdown(wait=False)

    def _new_app(self):
        import win32com.client

        # DispatchEx 必须用：Dispatch 会 attach 到用户已经开着的实例，之后的 Quit
        # 会连人家没存的文档一起关掉。
        app = win32com.client.DispatchEx(self.prog_id)
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

        探测也会 DispatchEx 起一个 Word，超时清理也走进程差集——探测与更新并发时，
        探测超时照样会杀掉别人正在跑的更新。凡是起 Word 的动作都必须串行。

        代价是：另一份报告正在更新域时，生成前检查（设计 §16 第 9 条）会等在队列里。
        这是可接受的——那时本来也轮不到你用 Word。
        """

        def work() -> ProbeResult:
            try:
                app = self._new_app()
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

        def work() -> int | None:
            app = self._new_app()
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
            output_docx.unlink(missing_ok=True)
            raise
        except Exception as exc:
            output_docx.unlink(missing_ok=True)
            raise FieldUpdateError(
                code="REPORT_FIELD_UPDATE_FAILED",
                message=f"{self.name} 更新域失败：{exc}",
                stage="update",
                updater=self.name,
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
