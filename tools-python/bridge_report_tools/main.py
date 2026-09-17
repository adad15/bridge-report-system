from pathlib import Path

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, ConfigDict

from bridge_report_tools import __version__
from bridge_report_tools.config import get_settings
from bridge_report_tools.importers.word_context import WordImportRequest, WordImportResponse
from bridge_report_tools.importers.word_errors import WordImportError
from bridge_report_tools.importers.source_db.context import (
    SourceImportRequest,
    parse_source_import,
)
from bridge_report_tools.importers.source_db.reader import (
    SourceDatabaseError,
    load_task_summaries,
    open_source_db,
)
from bridge_report_tools.importers.word_importer import parse_word_import
from bridge_report_tools.maps import (
    StaticMapError,
    StaticMapRequest,
    StaticMapResponse,
    fetch_static_map,
)
from bridge_report_tools.reports.errors import ReportBuildError, ReportTemplateError
from bridge_report_tools.reports.field_update_queue import FieldUpdateQueueTimeout
from bridge_report_tools.reports.field_updater import FieldUpdateError
from bridge_report_tools.reports.service import (
    FieldUpdateRequest,
    FieldUpdateResponse,
    OutputValidationRequest,
    OutputValidationResponse,
    ReportAssembleRequest,
    ReportAssembleResponse,
    TemplateValidationRequest,
    TemplateValidationResponse,
    assemble_report_request,
    update_fields_request,
    validate_output_request,
    validate_template_request,
)


class HealthResponse(BaseModel):
    status: str
    service: str
    version: str
    host: str
    port: int


app = FastAPI(
    title="Bridge Report Python Tools",
    version=__version__,
)


@app.get("/health", response_model=HealthResponse)
def health() -> HealthResponse:
    settings = get_settings()
    return HealthResponse(
        status="ok",
        service="bridge-report-python-tools",
        version=__version__,
        host=settings.host,
        port=settings.port,
    )


@app.post("/imports/word/parse", response_model=WordImportResponse)
def parse_word(request: WordImportRequest) -> WordImportResponse:
    try:
        return parse_word_import(request)
    except WordImportError as exc:
        raise HTTPException(
            status_code=400,
            detail={
                "code": exc.code,
                "message": exc.message,
            },
        ) from exc


@app.post("/imports/source/parse", response_model=WordImportResponse)
def parse_source(request: SourceImportRequest) -> WordImportResponse:
    """从来源软件的本机离线库导入。

    响应形状与 /imports/word/parse 完全相同——契约是两条路的解耦层，后端只认它。
    """
    try:
        return parse_source_import(request)
    except SourceDatabaseError as exc:
        raise HTTPException(
            status_code=400,
            detail={
                "code": exc.code,
                "message": exc.message,
            },
        ) from exc


class SourceTasksRequest(BaseModel):
    model_config = ConfigDict(extra="forbid")

    source_db_path: Path


class SourceTaskRow(BaseModel):
    task_id: str
    name: str
    check_date: str | None
    defect_count: int
    photo_count: int


class SourceTasksResponse(BaseModel):
    tasks: list[SourceTaskRow]


@app.post("/imports/source/tasks", response_model=SourceTasksResponse)
def list_source_tasks(request: SourceTasksRequest) -> SourceTasksResponse:
    """列出离线库里有哪些检测任务。

    taskId 是厂商库里的 UUID，用户不可能知道；导入界面得先拿这张表让人选。
    """
    try:
        with open_source_db(request.source_db_path) as db:
            summaries = load_task_summaries(db)
    except SourceDatabaseError as exc:
        raise HTTPException(
            status_code=400,
            detail={"code": exc.code, "message": exc.message},
        ) from exc
    return SourceTasksResponse(tasks=[
        SourceTaskRow(
            task_id=summary.id,
            name=summary.name,
            check_date=summary.check_date,
            defect_count=summary.defect_count,
            photo_count=summary.photo_count,
        )
        for summary in summaries
    ])


@app.post("/reports/templates/validate", response_model=TemplateValidationResponse)
def validate_report_template(
    request: TemplateValidationRequest,
) -> TemplateValidationResponse:
    """校验一份上传的报告模板是否符合契约（设计 §7、§23.1）。

    "校验不通过"是正常结果，走 200 带 status=invalid 返回明细——管理员需要看到
    每一条问题出在模板哪里。只有连明细都出不来的情况（不是 zip、缺 document.xml、
    包本身不安全）才抛 400。
    """
    try:
        return validate_template_request(request)
    except ReportTemplateError as exc:
        raise HTTPException(
            status_code=400,
            detail={"code": exc.code, "message": exc.message},
        ) from exc
    except KeyError as exc:
        raise HTTPException(
            status_code=400,
            detail={
                "code": "template_contract_unknown",
                "message": f"未知的模板契约类型：{request.contract_type}",
            },
        ) from exc


@app.post("/reports/assemble", response_model=ReportAssembleResponse)
def assemble_report(request: ReportAssembleRequest) -> ReportAssembleResponse:
    """装配阶段：把上下文装配进模板，产出还没刷域的中间文档（设计 §18）。

    装不出来一律 400 带错误码中止，不交一份缺内容的文件——报告是交付物，缺内容比
    不出报告危险得多。
    """
    try:
        return assemble_report_request(request)
    except ReportBuildError as exc:
        raise HTTPException(
            status_code=400,
            detail={"code": exc.code, "message": exc.message},
        ) from exc


@app.post("/reports/fields/update", response_model=FieldUpdateResponse)
def update_report_fields(request: FieldUpdateRequest) -> FieldUpdateResponse:
    """刷域阶段：交给 Word 或 WPS 算目录页码和总页数（设计 §19）。

    本机同时只允许一个更新在跑，这里会排队。排不上（503）与更新本身失败（502）
    分开：前者是"再等等"，后者是这台机器上的 Office 出了问题，重试无用。
    """
    try:
        return update_fields_request(request)
    except FieldUpdateQueueTimeout as exc:
        raise HTTPException(
            status_code=503,
            detail={
                "code": exc.code,
                "message": exc.message,
                "waited_seconds": exc.waited_seconds,
            },
        ) from exc
    except FieldUpdateError as exc:
        raise HTTPException(
            status_code=502,
            detail={
                "code": exc.code,
                "message": exc.message,
                "stage": exc.stage,
                "updater": exc.updater,
            },
        ) from exc


@app.post("/reports/output/validate", response_model=OutputValidationResponse)
def validate_report_output(request: OutputValidationRequest) -> OutputValidationResponse:
    """校验阶段：交付前的最后一道关（设计 §20）。

    与模板校验一样，"没通过"是正常结果，走 200 带明细返回；只有连明细都出不来
    （文件不存在）才 400。
    """
    try:
        return validate_output_request(request)
    except ReportTemplateError as exc:
        raise HTTPException(
            status_code=400,
            detail={"code": exc.code, "message": exc.message},
        ) from exc
    except KeyError as exc:
        raise HTTPException(
            status_code=400,
            detail={
                "code": "template_contract_unknown",
                "message": f"unknown contract: {request.contract_type}",
            },
        ) from exc


@app.post("/map/static-image", response_model=StaticMapResponse)
def static_map_image(request: StaticMapRequest) -> StaticMapResponse:
    """按给定取景取一张静态地图，给报告 §1.1 的图 1-1 用。

    本机这套 drogon 的 HTTPS 客户端连不出去，所以这一步放在工具服务这边。
    外部服务不好使是常态，报错要说清楚是连不上还是 key 不对，因为两者的修法完全不同。
    """
    try:
        return fetch_static_map(request)
    except StaticMapError as exc:
        raise HTTPException(
            status_code=502,
            detail={"code": exc.code, "message": exc.message},
        ) from exc
